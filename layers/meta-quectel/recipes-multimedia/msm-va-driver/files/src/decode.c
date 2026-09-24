// SPDX-License-Identifier: MIT
#include "backend_internal.h"

#include "h264_annexb.h"

#include <errno.h>
#include <limits.h>
#include <linux/videodev2.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <va/va_dec_hevc.h>
#include <va/va_dec_vp9.h>

#define VENUS_MAX_SLICE_BATCHES 64
#define VENUS_ACCESS_UNIT_OVERHEAD 4096u
#define VENUS_SYNC_TIMEOUT_MS 30000
/* How long both sides must have been quiet before a stalled surface sync is
 * allowed to drain the decoder.  See
 * sync_surface_locked() for why this exists.
 */
#define VENUS_QUIET_MS_BEFORE_DRAIN 1000

static int64_t monotonic_milliseconds(void)
{
    struct timespec timestamp;

    if (clock_gettime(CLOCK_MONOTONIC, &timestamp) != 0)
        return -1;
    return (int64_t)timestamp.tv_sec * 1000 +
           timestamp.tv_nsec / 1000000;
}

static void clear_pending(struct venus_context *context)
{
    context->pending_count = 0;
}

static void destroy_context_buffers(struct venus_backend *backend,
                                    VAContextID context_id)
{
    unsigned int index;

    for (index = 0; index < VENUS_MAX_BUFFERS; index++) {
        if (backend->buffers[index].used &&
            backend->buffers[index].context_id == context_id)
            venus_backend_free_buffer(&backend->buffers[index]);
    }
}

int venus_decode_store_frame_locked(
    const struct venus_v4l2_frame *frame, void *opaque)
{
    struct venus_context *context = opaque;
    struct venus_backend *backend = context ? context->backend : NULL;
    struct venus_surface *surface;
    uint8_t *resized;
    uint32_t stride;
    size_t source_luma_size;
    size_t source_size;
    size_t destination_luma_size;
    size_t destination_size;
    unsigned int row;

    if (!backend || !frame || frame->tag > UINT32_MAX ||
        frame->width == 0 || frame->height == 0 ||
        (frame->width & 1u) || (frame->height & 1u))
        return -EINVAL;

    surface = venus_backend_find_surface(
        backend, (VASurfaceID)frame->tag);
    if (!surface)
        return -ENOENT;
    if (surface->width > frame->width ||
        surface->height > frame->height)
        return -EINVAL;

    stride = frame->bytes_per_line
                 ? frame->bytes_per_line
                 : frame->width;
    if (stride < frame->width ||
        stride > SIZE_MAX / frame->height)
        return -EINVAL;

    source_luma_size = (size_t)stride * frame->height;
    if (source_luma_size >
        SIZE_MAX - (size_t)stride * (frame->height / 2))
        return -EOVERFLOW;
    source_size =
        source_luma_size +
        (size_t)stride * (frame->height / 2);
    if (frame->size < source_size ||
        surface->width > SIZE_MAX / surface->height)
        return -EINVAL;

    destination_luma_size =
        (size_t)surface->width * surface->height;
    if (destination_luma_size >
        SIZE_MAX - destination_luma_size / 2)
        return -EOVERFLOW;
    destination_size =
        destination_luma_size +
        destination_luma_size / 2;

    if (destination_size > surface->capacity) {
        resized = realloc(surface->data, destination_size);
        if (!resized)
            return -ENOMEM;
        surface->data = resized;
        surface->capacity = destination_size;
    }

    for (row = 0; row < surface->height; row++)
        memcpy(surface->data +
                   (size_t)row * surface->width,
               frame->data + (size_t)row * stride,
               surface->width);

    for (row = 0; row < surface->height / 2; row++)
        memcpy(surface->data + destination_luma_size +
                   (size_t)row * surface->width,
               frame->data + source_luma_size +
                   (size_t)row * stride,
               surface->width);

    surface->data_size = destination_size;
    surface->ready = true;
    context->received_frames++;
    context->last_activity_ms = monotonic_milliseconds();
    venus_backend_log(
        backend,
        "capture surface=0x%x tag=%llu bytes=%zu visible=%ux%u coded=%ux%u stride=%u",
        surface->id, (unsigned long long)frame->tag,
        destination_size, surface->width, surface->height,
        frame->width, frame->height, stride);
    return 0;
}

static VAStatus backend_create_context(
    VADriverContextP driver_context, VAConfigID config_id,
    int picture_width, int picture_height, int flags,
    VASurfaceID *render_targets, int num_render_targets,
    VAContextID *result)
{
    struct venus_backend *backend =
        venus_backend_from_context(driver_context);
    struct venus_v4l2_decoder_config decoder_config;
    struct venus_v4l2_error decoder_error;
    struct venus_config *config;
    struct venus_context *context = NULL;
    enum venus_codec codec;
    unsigned int index;
    int status;

    (void)flags;

    if (!backend || !result || picture_width < 0 ||
        picture_height < 0 || num_render_targets < 0 ||
        (num_render_targets > 0 && !render_targets))
        return VA_STATUS_ERROR_INVALID_PARAMETER;
    if ((unsigned int)picture_width < VENUS_MIN_WIDTH ||
        (unsigned int)picture_height < VENUS_MIN_HEIGHT ||
        (unsigned int)picture_width > VENUS_MAX_WIDTH ||
        (unsigned int)picture_height > VENUS_MAX_HEIGHT)
        return VA_STATUS_ERROR_RESOLUTION_NOT_SUPPORTED;

    pthread_mutex_lock(&backend->mutex);
    config = venus_backend_find_config(backend, config_id);
    if (!config) {
        pthread_mutex_unlock(&backend->mutex);
        return VA_STATUS_ERROR_INVALID_CONFIG;
    }

    for (index = 0; index < (unsigned int)num_render_targets; index++) {
        struct venus_surface *surface =
            venus_backend_find_surface(backend, render_targets[index]);

        if (!surface || surface->width != (unsigned int)picture_width ||
            surface->height != (unsigned int)picture_height) {
            pthread_mutex_unlock(&backend->mutex);
            return VA_STATUS_ERROR_INVALID_SURFACE;
        }
    }

    for (index = 0; index < VENUS_MAX_CONTEXTS; index++) {
        if (!backend->contexts[index].used) {
            context = &backend->contexts[index];
            break;
        }
    }
    if (!context) {
        pthread_mutex_unlock(&backend->mutex);
        return VA_STATUS_ERROR_MAX_NUM_EXCEEDED;
    }

    if (config->entrypoint == VAEntrypointVLD) {
        if (!venus_backend_profile_codec(config->profile, &codec)) {
            pthread_mutex_unlock(&backend->mutex);
            return VA_STATUS_ERROR_UNSUPPORTED_PROFILE;
        }

        decoder_config = (struct venus_v4l2_decoder_config) {
            .device = backend->capabilities.decoder_path,
            .coded_format = venus_codec_fourcc(codec),
            .width = (uint32_t)picture_width,
            .height = (uint32_t)picture_height,
            .output_buffer_size = 2u * 1024u * 1024u,
            .output_buffers = 4,
            .capture_buffers = 16,
        };

        status = venus_v4l2_decoder_open(
            &decoder_config, &context->decoder, &decoder_error);
        if (status < 0) {
            venus_backend_log(
                backend,
                "create-context decoder-open failed operation=%s error=%d",
                decoder_error.operation[0]
                    ? decoder_error.operation
                    : "none",
                -status);
            pthread_mutex_unlock(&backend->mutex);
            return venus_backend_status_from_errno(status);
        }
    } else if (config->entrypoint != VAEntrypointEncSlice) {
        pthread_mutex_unlock(&backend->mutex);
        return VA_STATUS_ERROR_UNSUPPORTED_ENTRYPOINT;
    }

    context->used = true;
    context->id =
        VENUS_CONTEXT_BASE | (unsigned int)(context - backend->contexts + 1);
    context->config_id = config_id;
    context->width = (unsigned int)picture_width;
    context->backend = backend;
    context->height = (unsigned int)picture_height;
    context->target = VA_INVALID_ID;

    for (index = 0; index < (unsigned int)num_render_targets; index++) {
        struct venus_surface *surface =
            venus_backend_find_surface(backend, render_targets[index]);

        surface->context_id = context->id;
    }

    *result = context->id;
    venus_backend_log(backend,
                      "create-context id=0x%x config=0x%x size=%ux%u targets=%d",
                      context->id, config_id, context->width,
                      context->height, num_render_targets);
    pthread_mutex_unlock(&backend->mutex);
    return VA_STATUS_SUCCESS;
}

static void destroy_context_locked(struct venus_backend *backend,
                                   struct venus_context *context)
{
    unsigned int index;

    if (!context || !context->used)
        return;

    venus_v4l2_decoder_close(context->decoder);
    venus_encode_close_context(context);
    clear_pending(context);
    destroy_context_buffers(backend, context->id);

    for (index = 0; index < VENUS_MAX_SURFACES; index++) {
        if (backend->surfaces[index].used &&
            backend->surfaces[index].context_id == context->id) {
            backend->surfaces[index].context_id = VA_INVALID_ID;
            backend->surfaces[index].encode_pending = false;
            backend->surfaces[index].coded_buffer_id = VA_INVALID_ID;
        }
    }

    memset(context, 0, sizeof(*context));
}

static VAStatus backend_destroy_context(VADriverContextP driver_context,
                                        VAContextID context_id)
{
    struct venus_backend *backend =
        venus_backend_from_context(driver_context);
    struct venus_context *context;

    if (!backend)
        return VA_STATUS_ERROR_INVALID_CONTEXT;

    pthread_mutex_lock(&backend->mutex);
    context = venus_backend_find_context(backend, context_id);
    if (!context) {
        pthread_mutex_unlock(&backend->mutex);
        return VA_STATUS_ERROR_INVALID_CONTEXT;
    }

    destroy_context_locked(backend, context);
    pthread_mutex_unlock(&backend->mutex);
    return VA_STATUS_SUCCESS;
}

static VAStatus backend_begin_picture(VADriverContextP driver_context,
                                      VAContextID context_id,
                                      VASurfaceID render_target)
{
    struct venus_backend *backend =
        venus_backend_from_context(driver_context);
    struct venus_context *context;
    struct venus_config *config;
    struct venus_surface *surface;

    if (!backend)
        return VA_STATUS_ERROR_INVALID_CONTEXT;

    pthread_mutex_lock(&backend->mutex);
    context = venus_backend_find_context(backend, context_id);
    surface = venus_backend_find_surface(backend, render_target);
    if (!context) {
        pthread_mutex_unlock(&backend->mutex);
        return VA_STATUS_ERROR_INVALID_CONTEXT;
    }
    config = venus_backend_find_config(backend, context->config_id);
    if (!config) {
        pthread_mutex_unlock(&backend->mutex);
        return VA_STATUS_ERROR_INVALID_CONFIG;
    }
    if (!surface ||
        (config->entrypoint == VAEntrypointVLD &&
         (surface->width != context->width ||
          surface->height != context->height)) ||
        (config->entrypoint == VAEntrypointEncSlice &&
         (surface->width > context->width ||
          surface->height > context->height))) {
        pthread_mutex_unlock(&backend->mutex);
        venus_backend_log(
            backend,
            "begin-picture REJECT unknown/oversize surface=0x%x found=%d "
            "surf=%ux%u ctx=%ux%u entrypoint=%d backend=%p",
            render_target, surface != NULL,
            surface ? surface->width : 0, surface ? surface->height : 0,
            context->width, context->height, config->entrypoint,
            (void *)backend);
        return VA_STATUS_ERROR_INVALID_SURFACE;
    }
    if (context->in_picture || surface->encode_pending) {
        pthread_mutex_unlock(&backend->mutex);
        return VA_STATUS_ERROR_OPERATION_FAILED;
    }
    if (config->entrypoint == VAEntrypointEncSlice &&
        (!surface->ready || surface->data_size == 0)) {
        /*
         * The client can reach the encoder before the picture being encoded
         * has left the decoder's reorder buffer - ffmpeg hands the surface
         * straight from vaEndPicture() to the encoder without a sync.  The
         * frame is on its way, so wait for it (see
         * venus_wait_surface_locked()) instead of failing the encode; the
         * log below then only fires when the data really is not coming.
         */
        (void)venus_wait_surface_locked(
            backend, surface, VENUS_SURFACE_WAIT_TIMEOUT_MS);
        venus_backend_log(
            backend,
            "begin-picture wait surface=0x%x ready=%d data_size=%zu",
            render_target, surface->ready, surface->data_size);
    }
    if (config->entrypoint == VAEntrypointEncSlice &&
        (!surface->ready || surface->data_size == 0)) {
        pthread_mutex_unlock(&backend->mutex);
        venus_backend_log(
            backend,
            "begin-picture REJECT %s surface=0x%x ready=%d data_size=%zu "
            "encode_pending=%d surf_ctx=0x%x ctx=0x%x backend=%p",
            surface->ready ? "empty-data" : "not-ready", render_target,
            surface->ready, surface->data_size, surface->encode_pending,
            surface->context_id, context_id, (void *)backend);
        return VA_STATUS_ERROR_INVALID_SURFACE;
    }

    context->in_picture = true;
    context->target = render_target;
    context->pending_count = 0;
    surface->context_id = context_id;
    if (config->entrypoint == VAEntrypointVLD) {
        surface->ready = false;
        surface->data_size = 0;
    }
    venus_backend_log(backend,
                      "begin-picture context=0x%x surface=0x%x entrypoint=%d",
                      context_id, render_target, config->entrypoint);

    pthread_mutex_unlock(&backend->mutex);
    return VA_STATUS_SUCCESS;
}

static VAStatus backend_render_picture(VADriverContextP driver_context,
                                       VAContextID context_id,
                                       VABufferID *buffers,
                                       int num_buffers)
{
    struct venus_backend *backend =
        venus_backend_from_context(driver_context);
    struct venus_context *context;
    int index;

    if (!backend || num_buffers < 0 ||
        (num_buffers > 0 && !buffers))
        return VA_STATUS_ERROR_INVALID_PARAMETER;

    pthread_mutex_lock(&backend->mutex);
    context = venus_backend_find_context(backend, context_id);
    if (!context || !context->in_picture) {
        pthread_mutex_unlock(&backend->mutex);
        return VA_STATUS_ERROR_INVALID_CONTEXT;
    }
    if (context->pending_count + (size_t)num_buffers >
        VENUS_MAX_PENDING_BUFFERS) {
        pthread_mutex_unlock(&backend->mutex);
        return VA_STATUS_ERROR_MAX_NUM_EXCEEDED;
    }

    for (index = 0; index < num_buffers; index++) {
        struct venus_buffer *buffer =
            venus_backend_find_buffer(backend, buffers[index]);

        if (!buffer || buffer->context_id != context_id) {
            pthread_mutex_unlock(&backend->mutex);
            return VA_STATUS_ERROR_INVALID_BUFFER;
        }
    }

    for (index = 0; index < num_buffers; index++)
        context->pending[context->pending_count++] = buffers[index];

    pthread_mutex_unlock(&backend->mutex);
    return VA_STATUS_SUCCESS;
}

/* The parameter buffer the client sends for a codec, used to reject a buffer
 * too small to be one.  VP9 and HEVC picture parameters are ignored beyond
 * that: this decoder is handed a bitstream, not parsed fields.
 */
static size_t picture_parameter_size(enum venus_codec codec)
{
    switch (codec) {
    case VENUS_CODEC_H264:
        return sizeof(VAPictureParameterBufferH264);
    case VENUS_CODEC_HEVC:
        return sizeof(VAPictureParameterBufferHEVC);
    case VENUS_CODEC_VP9:
        return sizeof(VADecPictureParameterBufferVP9);
    default:
        return 0;
    }
}

static int collect_frame_buffers(
    struct venus_backend *backend, struct venus_context *context,
    enum venus_codec codec, const void **picture,
    struct venus_slice_batch *batches, size_t *num_batches,
    size_t *access_unit_capacity)
{
    struct venus_buffer *slice_parameters = NULL;
    size_t index;

    *picture = NULL;
    *num_batches = 0;
    *access_unit_capacity = VENUS_ACCESS_UNIT_OVERHEAD;

    for (index = 0; index < context->pending_count; index++) {
        struct venus_buffer *buffer = venus_backend_find_buffer(
            backend, context->pending[index]);
        size_t buffer_size;

        if (!buffer || buffer->element_size >
                           SIZE_MAX / buffer->num_elements)
            return -EINVAL;
        buffer_size = buffer->element_size * buffer->num_elements;

        switch (buffer->type) {
        case VAPictureParameterBufferType:
            if (*picture || buffer->num_elements != 1 ||
                buffer->element_size < picture_parameter_size(codec))
                return -EINVAL;
            if (codec == VENUS_CODEC_H264 || codec == VENUS_CODEC_HEVC)
                *picture = buffer->data;
            break;
        case VAIQMatrixBufferType:
            break;
        case VASliceParameterBufferType:
            if (slice_parameters || buffer->num_elements == 0 ||
                buffer->element_size <
                    sizeof(struct venus_slice_parameters))
                return -EINVAL;
            slice_parameters = buffer;
            break;
        case VASliceDataBufferType: {
            /* VP9 frames are not split into NAL units: the buffer is the
             * frame, so any slice parameters are for the decoder's benefit
             * and not for this assembly step.
             */
            bool with_slices = slice_parameters &&
                               codec != VENUS_CODEC_VP9;

            if (*num_batches >= VENUS_MAX_SLICE_BATCHES)
                return -EINVAL;
            if (codec == VENUS_CODEC_H264 && !slice_parameters)
                return -EINVAL;
            batches[*num_batches] =
                (struct venus_slice_batch) {
                    .parameters =
                        with_slices
                            ? (const struct venus_slice_parameters *)
                                  slice_parameters->data
                            : NULL,
                    .num_parameters =
                        with_slices ? slice_parameters->num_elements : 0,
                    .data = buffer->data,
                    .data_size = buffer_size,
                };
            (*num_batches)++;
            slice_parameters = NULL;
            if (buffer_size > SIZE_MAX - *access_unit_capacity)
                return -EOVERFLOW;
            *access_unit_capacity += buffer_size;
            break;
        }
        default:
            return -ENOTSUP;
        }
    }

    if (*num_batches == 0 || slice_parameters)
        return -EINVAL;
    if (codec == VENUS_CODEC_H264 && !*picture)
        return -EINVAL;
    return 0;
}

/*
 * The slice the decoder should be handed first, which is where the parameter
 * set identifier of HEVC is read from.
 */
static const uint8_t *first_slice(
    const struct venus_slice_batch *batches, size_t *size)
{
    if (batches->parameters && batches->num_parameters) {
        *size = batches->parameters[0].slice_data_size;
        return batches->data + batches->parameters[0].slice_data_offset;
    }

    *size = batches->data_size;
    return batches->data;
}

/*
 * Turn the buffers of one picture into the access unit the decoder wants.
 * H.264 needs its parameter sets rebuilt from the parsed picture parameters
 * (the client sends no raw headers); HEVC does too, but only once per
 * sequence, since the decoder keeps them; VP9 and the HEVC slices carry
 * everything else themselves.
 */
static int build_access_unit(
    struct venus_context *context, enum venus_codec codec, VAProfile profile,
    const void *picture, const struct venus_slice_batch *batches,
    size_t num_batches, uint8_t *output, size_t output_capacity,
    size_t *output_size)
{
    struct venus_annexb_writer writer = {
        .data = output,
        .capacity = output_capacity,
    };
    size_t index;
    int status = 0;

    if (codec == VENUS_CODEC_H264)
        return venus_h264_build_access_unit(
            profile, picture, batches, num_batches, output,
            output_capacity, output_size);

    if (codec == VENUS_CODEC_HEVC) {
        const VAPictureParameterBufferHEVC *parameters = picture;
        const struct venus_hevc_sequence sequence = {
            .picture = parameters,
            .coded_width = parameters->pic_width_in_luma_samples,
            .coded_height = parameters->pic_height_in_luma_samples,
            .visible_width = context->width,
            .visible_height = context->height,
        };
        uint8_t headers[VENUS_HEVC_HEADERS_MAX];
        struct venus_annexb_writer header_writer = {
            .data = headers,
            .capacity = sizeof(headers),
        };
        size_t slice_size;
        const uint8_t *slice = first_slice(batches, &slice_size);
        const unsigned int pps_id = venus_hevc_pps_id(
            slice, slice_size,
            parameters->slice_parsing_fields.bits.RapPicFlag != 0);

        status = venus_hevc_write_parameter_sets(
            &header_writer, &sequence, pps_id);
        if (status < 0)
            return status;

        if (header_writer.length != context->hevc_headers_size ||
            memcmp(headers, context->hevc_headers,
                   header_writer.length) != 0) {
            status = venus_annexb_write_bytes(
                &writer, headers, header_writer.length);
            if (status < 0)
                return status;
            memcpy(context->hevc_headers, headers, header_writer.length);
            context->hevc_headers_size = header_writer.length;
        }
    }

    for (index = 0; index < num_batches && status == 0; index++)
        status = venus_annexb_write_batch(&writer, &batches[index]);

    *output_size = writer.length;
    return status;
}

static VAStatus backend_end_picture(VADriverContextP driver_context,
                                    VAContextID context_id)
{
    struct venus_slice_batch batches[VENUS_MAX_SLICE_BATCHES];
    const void *picture;
    struct venus_backend *backend =
        venus_backend_from_context(driver_context);
    struct venus_context *context;
    struct venus_config *config;
    enum venus_codec codec;
    uint8_t *access_unit = NULL;
    size_t access_unit_capacity;
    size_t access_unit_size = 0;
    size_t num_batches;
    int status;

    if (!backend)
        return VA_STATUS_ERROR_INVALID_CONTEXT;

    pthread_mutex_lock(&backend->mutex);
    context = venus_backend_find_context(backend, context_id);
    if (!context || !context->in_picture) {
        pthread_mutex_unlock(&backend->mutex);
        return VA_STATUS_ERROR_INVALID_CONTEXT;
    }

    config = venus_backend_find_config(backend, context->config_id);
    if (!config) {
        status = -EINVAL;
        goto finish;
    }

    if (config->entrypoint == VAEntrypointEncSlice) {
        VAStatus encode_status = venus_encode_end_picture_locked(
            backend, context, config);

        clear_pending(context);
        context->in_picture = false;
        context->target = VA_INVALID_ID;
        if (encode_status != VA_STATUS_SUCCESS)
            venus_backend_log(
                backend,
                "end-picture encode failed context=0x%x status=%d",
                context_id, encode_status);
        pthread_mutex_unlock(&backend->mutex);
        return encode_status;
    }
    if (config->entrypoint != VAEntrypointVLD) {
        status = -ENOTSUP;
        goto finish;
    }

    if (!venus_backend_profile_codec(config->profile, &codec)) {
        status = -EINVAL;
        goto finish;
    }

    status = collect_frame_buffers(
        backend, context, codec, &picture, batches, &num_batches,
        &access_unit_capacity);
    if (status < 0)
        goto finish;

    access_unit = malloc(access_unit_capacity);
    if (!access_unit) {
        status = -ENOMEM;
        goto finish;
    }

    status = build_access_unit(
        context, codec, config->profile, picture, batches, num_batches,
        access_unit, access_unit_capacity, &access_unit_size);
    if (status < 0)
        goto finish;

    venus_backend_log(backend,
                      "end-picture context=0x%x surface=0x%x codec=%s batches=%zu access-unit=%zu",
                      context_id, context->target, venus_codec_name(codec),
                      num_batches, access_unit_size);
    status = venus_v4l2_decoder_submit(
        context->decoder, access_unit, access_unit_size,
        context->target, venus_decode_store_frame_locked, context);
    if (status == 0) {
        context->submitted_pictures++;
        context->last_activity_ms = monotonic_milliseconds();
        /* New input: a later stall is a new deadlock, not the one we
         * already drained for.
         */
        context->drained = false;
    }

finish:
    free(access_unit);
    clear_pending(context);
    context->in_picture = false;
    context->target = VA_INVALID_ID;
    if (status < 0)
        venus_backend_log(backend,
                          "end-picture failed context=0x%x error=%d",
                          context_id, -status);
    pthread_mutex_unlock(&backend->mutex);
    return status < 0 ? venus_backend_status_from_errno(status)
                      : VA_STATUS_SUCCESS;
}

/*
 * Force the decoder to hand back the frames it is holding, then put the
 * session back to work: V4L2_DEC_CMD_STOP flushes the reorder buffer, and
 * V4L2_DEC_CMD_START lets the client keep feeding pictures afterwards.
 * Without the resume the session is dead for any further picture, which is
 * why the drain is only safe as a stop/start pair.
 */
static int drain_decoder_locked(struct venus_backend *backend,
                                struct venus_context *context)
{
    bool end_of_stream = false;
    unsigned int guard = 0;
    int status;

    status = venus_v4l2_decoder_stop(context->decoder);
    if (status < 0)
        return status;

    while (!end_of_stream && guard++ < 200) {
        status = venus_v4l2_decoder_pump(
            context->decoder, 1000, venus_decode_store_frame_locked,
            context, &end_of_stream);
        if (status < 0 && status != -ETIMEDOUT && status != -EAGAIN)
            break;
    }

    status = venus_v4l2_decoder_resume(context->decoder);
    if (status < 0)
        return status;

    context->drained = true;
    venus_backend_log(
        backend, "decoder drained, %llu/%llu frames out%s",
        (unsigned long long)context->received_frames,
        (unsigned long long)context->submitted_pictures,
        end_of_stream ? "" : " (incomplete)");
    return 0;
}

static VAStatus sync_surface_locked(struct venus_backend *backend,
                                    struct venus_surface *surface,
                                    int timeout_ms)
{
    struct venus_context *context;
    int64_t deadline;

    if (surface->encode_pending)
        return venus_encode_sync_surface_locked(
            backend, surface, timeout_ms);
    if (surface->ready)
        return VA_STATUS_SUCCESS;

    context = venus_backend_find_context(
        backend, surface->context_id);
    if (!context)
        return VA_STATUS_ERROR_INVALID_CONTEXT;

    deadline = monotonic_milliseconds() + timeout_ms;
    while (!surface->ready &&
           monotonic_milliseconds() < deadline) {
        int status = venus_v4l2_decoder_pump(
            context->decoder, 1000,
            venus_decode_store_frame_locked, context, NULL);

        if (status < 0 && status != -ETIMEDOUT && status != -EAGAIN)
            return venus_backend_status_from_errno(status);

        /*
         * A stall here is usually harmless: a reordered frame is waiting for
         * pictures the client's decoder thread is still submitting, and the
         * wait ends by itself.  When the client has stopped submitting
         * altogether the decoder is instead holding frames it will never
         * release on its own, and so is the client: a stateful decoder emits
         * a reordered frame only after the next picture arrives, while the
         * client cannot submit that picture until a sync frees a surface
         * from its pool.  Draining is the only way out of that deadlock.
         *
         * ponytail: heuristic.  VA-API has no end-of-stream call, so "no
         * picture in and no picture out for 1s while a sync is pending"
         * stands in for it.  Both halves are needed - the client's decoder
         * thread keeps submitting while a reordered frame waits for its
         * references.  Raise VENUS_QUIET_MS_BEFORE_DRAIN if a client ever
         * pauses longer than that in the middle of one continuous stream.
         */
        if (!context->drained &&
            context->submitted_pictures > 0 &&
            context->last_activity_ms != 0 &&
            monotonic_milliseconds() - context->last_activity_ms >=
                VENUS_QUIET_MS_BEFORE_DRAIN) {
            int drain = drain_decoder_locked(backend, context);

            if (drain < 0)
                return venus_backend_status_from_errno(drain);
        }
    }

    if (!surface->ready)
        venus_backend_log(
            backend,
            "sync FAILED surface=0x%x ready=%d bytes=%zu submitted=%llu "
            "received=%llu in_flight=%llu",
            surface->id, surface->ready, surface->data_size,
            (unsigned long long)context->submitted_pictures,
            (unsigned long long)context->received_frames,
            (unsigned long long)(context->submitted_pictures -
                                 context->received_frames));
    return surface->ready ? VA_STATUS_SUCCESS
                          : VA_STATUS_ERROR_HW_BUSY;
}

/*
 * An encoder reads a surface the decoder filled, and ffmpeg does not
 * vaSyncSurface() a decoded surface: it hands the surface to the encoder as
 * soon as its decoder emits the frame, which can happen while the picture is
 * still sitting in the decoder's reorder buffer.  The data is on its way and
 * nothing else can take the surface over (the client still holds the surface
 * in its pool, the decoder cannot re-target a surface with encode_pending
 * set), so wait for it like a sync would instead of failing the client.
 */
VAStatus venus_wait_surface_locked(struct venus_backend *backend,
                                   struct venus_surface *surface,
                                   int timeout_ms)
{
    return sync_surface_locked(backend, surface, timeout_ms);
}

static VAStatus backend_sync_surface(VADriverContextP driver_context,
                                     VASurfaceID surface_id)
{
    struct venus_backend *backend =
        venus_backend_from_context(driver_context);
    struct venus_surface *surface;
    VAStatus status;

    if (!backend)
        return VA_STATUS_ERROR_INVALID_CONTEXT;

    pthread_mutex_lock(&backend->mutex);
    surface = venus_backend_find_surface(backend, surface_id);
    if (!surface) {
        pthread_mutex_unlock(&backend->mutex);
        return VA_STATUS_ERROR_INVALID_SURFACE;
    }

    status = sync_surface_locked(
        backend, surface, VENUS_SYNC_TIMEOUT_MS);
    venus_backend_log(backend,
                      "sync-surface id=0x%x status=%d bytes=%zu ready=%d",
                      surface_id, status, surface->data_size, surface->ready);
    pthread_mutex_unlock(&backend->mutex);
    return status;
}

static VAStatus backend_sync_surface2(
    VADriverContextP driver_context, VASurfaceID surface_id,
    uint64_t timeout_ns)
{
    struct venus_backend *backend =
        venus_backend_from_context(driver_context);
    struct venus_surface *surface;
    uint64_t timeout_ms;
    VAStatus status;

    if (!backend)
        return VA_STATUS_ERROR_INVALID_CONTEXT;

    timeout_ms = timeout_ns == VA_TIMEOUT_INFINITE
                     ? VENUS_SYNC_TIMEOUT_MS
                     : (timeout_ns + 999999u) / 1000000u;
    if (timeout_ms > INT_MAX)
        timeout_ms = INT_MAX;

    pthread_mutex_lock(&backend->mutex);
    surface = venus_backend_find_surface(backend, surface_id);
    if (!surface) {
        pthread_mutex_unlock(&backend->mutex);
        return VA_STATUS_ERROR_INVALID_SURFACE;
    }

    status = sync_surface_locked(
        backend, surface, (int)timeout_ms);
    pthread_mutex_unlock(&backend->mutex);
    return status;
}

static VAStatus backend_sync_buffer(
    VADriverContextP driver_context, VABufferID buffer_id,
    uint64_t timeout_ns)
{
    struct venus_backend *backend =
        venus_backend_from_context(driver_context);
    struct venus_buffer *buffer;
    uint64_t timeout_ms;
    VAStatus status;

    if (!backend)
        return VA_STATUS_ERROR_INVALID_CONTEXT;

    timeout_ms = timeout_ns == VA_TIMEOUT_INFINITE
                     ? VENUS_SYNC_TIMEOUT_MS
                     : (timeout_ns + 999999u) / 1000000u;
    if (timeout_ms > INT_MAX)
        timeout_ms = INT_MAX;

    pthread_mutex_lock(&backend->mutex);
    buffer = venus_backend_find_buffer(backend, buffer_id);
    if (!buffer || buffer->type != VAEncCodedBufferType) {
        pthread_mutex_unlock(&backend->mutex);
        return VA_STATUS_ERROR_INVALID_BUFFER;
    }

    status = venus_encode_sync_buffer_locked(
        backend, buffer, (int)timeout_ms);
    venus_backend_log(
        backend,
        "sync-buffer id=0x%x status=%d bytes=%zu",
        buffer_id, status, buffer->coded_size);
    pthread_mutex_unlock(&backend->mutex);
    return status;
}

static VAStatus backend_query_surface_status(
    VADriverContextP driver_context, VASurfaceID surface_id,
    VASurfaceStatus *surface_status)
{
    struct venus_backend *backend =
        venus_backend_from_context(driver_context);
    struct venus_surface *surface;

    if (!backend || !surface_status)
        return VA_STATUS_ERROR_INVALID_PARAMETER;

    pthread_mutex_lock(&backend->mutex);
    surface = venus_backend_find_surface(backend, surface_id);
    if (!surface) {
        pthread_mutex_unlock(&backend->mutex);
        return VA_STATUS_ERROR_INVALID_SURFACE;
    }

    *surface_status =
        surface->ready && !surface->encode_pending
            ? VASurfaceReady
            : VASurfaceRendering;
    pthread_mutex_unlock(&backend->mutex);
    return VA_STATUS_SUCCESS;
}

void venus_decode_destroy_all(struct venus_backend *backend)
{
    unsigned int index;

    for (index = 0; index < VENUS_MAX_CONTEXTS; index++)
        destroy_context_locked(backend, &backend->contexts[index]);
}

void venus_decode_fill_vtable(struct VADriverVTable *vtable)
{
    vtable->vaCreateContext = backend_create_context;
    vtable->vaDestroyContext = backend_destroy_context;
    vtable->vaBeginPicture = backend_begin_picture;
    vtable->vaRenderPicture = backend_render_picture;
    vtable->vaEndPicture = backend_end_picture;
    vtable->vaSyncSurface = backend_sync_surface;
    vtable->vaSyncSurface2 = backend_sync_surface2;
    vtable->vaSyncBuffer = backend_sync_buffer;
    vtable->vaQuerySurfaceStatus = backend_query_surface_status;
}
