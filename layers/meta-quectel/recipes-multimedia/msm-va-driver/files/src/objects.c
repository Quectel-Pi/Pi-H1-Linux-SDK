// SPDX-License-Identifier: MIT
#include "backend_internal.h"

#include <fcntl.h>
#include <limits.h>
#include <linux/dma-heap.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <va/va_drmcommon.h>

/* Defined locally so libdrm headers are not a build dependency: this driver
 * only ever exports one format, and both values are frozen ABI.
 */
#define VENUS_DRM_FORMAT_NV12 \
    VENUS_FOURCC('N', 'V', '1', '2')
/* Per-plane formats for the separate-layer export layout: luma is a single
 * byte per pixel, chroma is an interleaved pair, which is what DRM_FORMAT_GR88
 * means. */
#define VENUS_DRM_FORMAT_R8 \
    VENUS_FOURCC('R', '8', ' ', ' ')
#define VENUS_DRM_FORMAT_GR88 \
    VENUS_FOURCC('G', 'R', '8', '8')
#define VENUS_DRM_FORMAT_MOD_LINEAR 0ULL
#define VENUS_FOURCC(a, b, c, d)                     \
    ((uint32_t)(a) | ((uint32_t)(b) << 8) |          \
     ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))

/*
 * Surface backing store.
 *
 * Surfaces are allocated from a DMA-BUF heap so vaExportSurfaceHandle() can
 * hand applications a file descriptor for zero-copy/EGL import - that is what
 * mpv's vaapi hwdec and VLC's GL interop need.  They stay CPU-mappable, which
 * is what the copy-based download path in decode.c uses.
 *
 * Without an accessible heap the driver falls back to plain memory: every
 * VA-API entry point keeps working except the export ones, which report the
 * surface as unsupported rather than lying about it.
 */
static const char *const surface_heaps[] = {
    "/dev/dma_heap/qcom,system",
    "/dev/dma_heap/system",
};

static uint8_t *surface_memory_allocate(size_t size, int *fd_out)
{
    size_t i;

    *fd_out = -1;

    for (i = 0; i < sizeof(surface_heaps) / sizeof(surface_heaps[0]); i++) {
        struct dma_heap_allocation_data request = {
            .len = size,
            .fd_flags = O_RDWR | O_CLOEXEC,
        };
        uint8_t *mapping;
        int heap;

        heap = open(surface_heaps[i], O_RDWR | O_CLOEXEC);
        if (heap < 0)
            continue;
        if (ioctl(heap, DMA_HEAP_IOCTL_ALLOC, &request) < 0) {
            close(heap);
            continue;
        }
        close(heap);

        mapping = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED,
                       request.fd, 0);
        if (mapping == MAP_FAILED) {
            close(request.fd);
            continue;
        }

        memset(mapping, 0, size);
        *fd_out = (int)request.fd;
        return mapping;
    }

    return calloc(1, size);
}

static void surface_memory_release(uint8_t *data, int fd, size_t size)
{
    if (!data)
        return;

    if (fd < 0) {
        free(data);
        return;
    }

    munmap(data, size);
    close(fd);
}

static int nv12_size(unsigned int width, unsigned int height,
                     size_t *size)
{
    size_t pixels;

    if (width == 0 || height == 0 || width % 2 || height % 2 ||
        width > SIZE_MAX / height)
        return -1;

    pixels = (size_t)width * height;
    if (pixels > SIZE_MAX / 3 * 2)
        return -1;

    *size = pixels + pixels / 2;
    return 0;
}

static struct venus_buffer *allocate_buffer(
    struct venus_backend *backend, VAContextID context_id,
    VABufferType type, size_t element_size, size_t num_elements,
    const void *data, bool owns_data)
{
    size_t total_size;
    unsigned int index;

    if (element_size == 0 || num_elements == 0 ||
        element_size > SIZE_MAX / num_elements)
        return NULL;
    total_size = element_size * num_elements;

    for (index = 0; index < VENUS_MAX_BUFFERS; index++) {
        struct venus_buffer *buffer = &backend->buffers[index];

        if (buffer->used)
            continue;

        *buffer = (struct venus_buffer) {
            .used = true,
            .id = VENUS_BUFFER_BASE | (index + 1),
            .context_id = context_id,
            .type = type,
            .element_size = element_size,
            .capacity_elements = num_elements,
            .num_elements = num_elements,
            .owns_data = owns_data,
            .source_surface_id = VA_INVALID_ID,
        };

        if (owns_data) {
            buffer->data = malloc(total_size);
            if (!buffer->data) {
                memset(buffer, 0, sizeof(*buffer));
                return NULL;
            }
            if (data)
                memcpy(buffer->data, data, total_size);
            else
                memset(buffer->data, 0, total_size);
        } else {
            buffer->data = (uint8_t *)data;
        }

        return buffer;
    }

    return NULL;
}

static bool valid_surface_attributes(VASurfaceAttrib *attributes,
                                     unsigned int num_attributes,
                                     uint32_t *fourcc)
{
    unsigned int index;

    *fourcc = VA_FOURCC_NV12;
    for (index = 0; index < num_attributes; index++) {
        const VASurfaceAttrib *attribute = &attributes[index];

        if (attribute->type == VASurfaceAttribPixelFormat) {
            if (attribute->value.type != VAGenericValueTypeInteger ||
                attribute->value.value.i != VA_FOURCC_NV12)
                return false;
            *fourcc = (uint32_t)attribute->value.value.i;
        } else if (attribute->type == VASurfaceAttribMemoryType) {
            if (attribute->value.type != VAGenericValueTypeInteger ||
                !(attribute->value.value.i &
                  VA_SURFACE_ATTRIB_MEM_TYPE_VA))
                return false;
        } else if (attribute->type == VASurfaceAttribUsageHint) {
            /* Purely advisory: GStreamer's va plugin always sets
             * VA_SURFACE_ATTRIB_USAGE_HINT_DECODER. Nothing to do with it,
             * but rejecting it made every GStreamer decoder unusable. */
            if (attribute->value.type != VAGenericValueTypeInteger)
                return false;
        }
        /* Anything else is ignored, as the VA spec requires: only an
         * unsupported memory type is an error. */
    }

    return true;
}

/*
 * Surface creation requests that the driver refuses are worth logging with
 * their parameters: clients (FFmpeg's hwframe pool in particular) retry with
 * different attribute sets, and without the request on record the only
 * symptom is an opaque VA_STATUS_ERROR_*.
 */
static VAStatus reject_surfaces(struct venus_backend *backend,
                                unsigned int format, unsigned int width,
                                unsigned int height, unsigned int num_surfaces,
                                VASurfaceAttrib *attributes,
                                unsigned int num_attributes,
                                VAStatus status)
{
    unsigned int index;

    venus_backend_log(backend,
                      "create-surface rejected status=0x%x format=0x%x %ux%u "
                      "count=%u attributes=%u",
                      status, format, width, height, num_surfaces,
                      num_attributes);
    for (index = 0; index < num_attributes; index++)
        venus_backend_log(backend,
                          "  attribute[%u] type=%d flags=0x%x value_type=%d "
                          "value=%lld",
                          index, attributes[index].type,
                          attributes[index].flags,
                          attributes[index].value.type,
                          (long long)attributes[index].value.value.i);
    return status;
}

static VAStatus create_surfaces_locked(
    struct venus_backend *backend, unsigned int format,
    unsigned int width, unsigned int height,
    VASurfaceID *surface_ids, unsigned int num_surfaces,
    VASurfaceAttrib *attributes, unsigned int num_attributes)
{
    struct venus_surface *created[VENUS_MAX_SURFACES];
    size_t capacity;
    uint32_t fourcc;
    unsigned int free_slots = 0;
    unsigned int created_count = 0;
    unsigned int index;

    if (!(format & VA_RT_FORMAT_YUV420))
        return reject_surfaces(backend, format, width, height, num_surfaces,
                               attributes, num_attributes,
                               VA_STATUS_ERROR_UNSUPPORTED_RT_FORMAT);
    if (!surface_ids || num_surfaces == 0 ||
        num_surfaces > VENUS_MAX_SURFACES ||
        width < VENUS_MIN_SURFACE_WIDTH ||
        height < VENUS_MIN_SURFACE_HEIGHT ||
        width > VENUS_MAX_WIDTH || height > VENUS_MAX_HEIGHT ||
        nv12_size(width, height, &capacity) < 0)
        return reject_surfaces(backend, format, width, height, num_surfaces,
                               attributes, num_attributes,
                               VA_STATUS_ERROR_INVALID_PARAMETER);
    if (num_attributes > 0 && !attributes)
        return reject_surfaces(backend, format, width, height, num_surfaces,
                               attributes, num_attributes,
                               VA_STATUS_ERROR_INVALID_PARAMETER);
    if (!valid_surface_attributes(attributes, num_attributes, &fourcc))
        return reject_surfaces(backend, format, width, height, num_surfaces,
                               attributes, num_attributes,
                               VA_STATUS_ERROR_UNSUPPORTED_MEMORY_TYPE);

    for (index = 0; index < VENUS_MAX_SURFACES; index++) {
        if (!backend->surfaces[index].used)
            free_slots++;
    }
    if (free_slots < num_surfaces)
        return VA_STATUS_ERROR_MAX_NUM_EXCEEDED;

    for (index = 0; index < VENUS_MAX_SURFACES &&
                    created_count < num_surfaces;
         index++) {
        struct venus_surface *surface = &backend->surfaces[index];

        if (surface->used)
            continue;

        surface->data = surface_memory_allocate(
            capacity, &surface->dmabuf_fd);
        if (!surface->data) {
            surface->dmabuf_fd = -1;
            goto fail;
        }

        surface->used = true;
        surface->id = VENUS_SURFACE_BASE | (index + 1);
        surface->width = width;
        surface->height = height;
        surface->fourcc = fourcc;
        surface->capacity = capacity;
        surface->data_size = capacity;
        surface->ready = true;
        surface->coded_buffer_id = VA_INVALID_ID;
        surface->context_id = VA_INVALID_ID;
        created[created_count] = surface;
        surface_ids[created_count] = surface->id;
        venus_backend_log(
            backend,
            "create-surface id=0x%x size=%ux%u bytes=%zu dmabuf=%d",
            surface->id, width, height, capacity, surface->dmabuf_fd);
        created_count++;
    }

    return VA_STATUS_SUCCESS;

fail:
    while (created_count > 0) {
        struct venus_surface *surface = created[--created_count];

        surface_memory_release(
            surface->data, surface->dmabuf_fd, surface->capacity);
        memset(surface, 0, sizeof(*surface));
    }
    return VA_STATUS_ERROR_ALLOCATION_FAILED;
}

static VAStatus backend_create_surfaces(
    VADriverContextP context, int width, int height, int format,
    int num_surfaces, VASurfaceID *surfaces)
{
    struct venus_backend *backend =
        venus_backend_from_context(context);
    VAStatus status;

    if (!backend || width < 0 || height < 0 || num_surfaces < 0)
        return VA_STATUS_ERROR_INVALID_PARAMETER;

    pthread_mutex_lock(&backend->mutex);
    status = create_surfaces_locked(
        backend, (unsigned int)format, (unsigned int)width,
        (unsigned int)height, surfaces, (unsigned int)num_surfaces,
        NULL, 0);
    pthread_mutex_unlock(&backend->mutex);
    return status;
}

static VAStatus backend_create_surfaces2(
    VADriverContextP context, unsigned int format,
    unsigned int width, unsigned int height,
    VASurfaceID *surfaces, unsigned int num_surfaces,
    VASurfaceAttrib *attributes, unsigned int num_attributes)
{
    struct venus_backend *backend =
        venus_backend_from_context(context);
    VAStatus status;

    if (!backend)
        return VA_STATUS_ERROR_INVALID_CONTEXT;

    pthread_mutex_lock(&backend->mutex);
    status = create_surfaces_locked(
        backend, format, width, height, surfaces, num_surfaces,
        attributes, num_attributes);
    pthread_mutex_unlock(&backend->mutex);
    return status;
}

static VAStatus backend_destroy_surfaces(
    VADriverContextP context, VASurfaceID *surface_ids,
    int num_surfaces)
{
    struct venus_backend *backend =
        venus_backend_from_context(context);
    int index;

    if (!backend || num_surfaces < 0 ||
        (num_surfaces > 0 && !surface_ids))
        return VA_STATUS_ERROR_INVALID_PARAMETER;

    pthread_mutex_lock(&backend->mutex);
    for (index = 0; index < num_surfaces; index++) {
        if (!venus_backend_find_surface(
                backend, surface_ids[index])) {
            pthread_mutex_unlock(&backend->mutex);
            return VA_STATUS_ERROR_INVALID_SURFACE;
        }
    }

    for (index = 0; index < num_surfaces; index++) {
        struct venus_surface *surface =
            venus_backend_find_surface(backend, surface_ids[index]);

        surface_memory_release(
            surface->data, surface->dmabuf_fd, surface->capacity);
        memset(surface, 0, sizeof(*surface));
    }

    pthread_mutex_unlock(&backend->mutex);
    return VA_STATUS_SUCCESS;
}

static VAStatus backend_create_buffer(
    VADriverContextP context, VAContextID context_id,
    VABufferType type, unsigned int size,
    unsigned int num_elements, void *data,
    VABufferID *buffer_id)
{
    struct venus_backend *backend =
        venus_backend_from_context(context);
    struct venus_buffer *buffer;

    if (!backend || !buffer_id || size == 0 || num_elements == 0)
        return VA_STATUS_ERROR_INVALID_PARAMETER;

    pthread_mutex_lock(&backend->mutex);
    if (!venus_backend_find_context(backend, context_id)) {
        pthread_mutex_unlock(&backend->mutex);
        return VA_STATUS_ERROR_INVALID_CONTEXT;
    }

    buffer = allocate_buffer(
        backend, context_id, type, size, num_elements, data, true);
    if (!buffer) {
        pthread_mutex_unlock(&backend->mutex);
        return VA_STATUS_ERROR_ALLOCATION_FAILED;
    }

    if (type == VAEncCodedBufferType) {
        buffer->coded_segment.buf = buffer->data;
        buffer->coded_segment.next = NULL;
    }

    *buffer_id = buffer->id;
    venus_backend_log(backend,
                      "create-buffer id=0x%x context=0x%x type=%d elements=%u size=%u",
                      buffer->id, context_id, type, num_elements, size);
    pthread_mutex_unlock(&backend->mutex);
    return VA_STATUS_SUCCESS;
}

static VAStatus backend_set_num_elements(
    VADriverContextP context, VABufferID buffer_id,
    unsigned int num_elements)
{
    struct venus_backend *backend =
        venus_backend_from_context(context);
    struct venus_buffer *buffer;
    uint8_t *resized;
    size_t old_size;
    size_t new_size;

    if (!backend || num_elements == 0)
        return VA_STATUS_ERROR_INVALID_PARAMETER;

    pthread_mutex_lock(&backend->mutex);
    buffer = venus_backend_find_buffer(backend, buffer_id);
    if (!buffer || !buffer->owns_data ||
        buffer->type == VAEncCodedBufferType) {
        pthread_mutex_unlock(&backend->mutex);
        return VA_STATUS_ERROR_INVALID_BUFFER;
    }

    if (num_elements > SIZE_MAX / buffer->element_size) {
        pthread_mutex_unlock(&backend->mutex);
        return VA_STATUS_ERROR_ALLOCATION_FAILED;
    }

    if (num_elements > buffer->capacity_elements) {
        old_size = buffer->capacity_elements * buffer->element_size;
        new_size = (size_t)num_elements * buffer->element_size;
        resized = realloc(buffer->data, new_size);
        if (!resized) {
            pthread_mutex_unlock(&backend->mutex);
            return VA_STATUS_ERROR_ALLOCATION_FAILED;
        }
        memset(resized + old_size, 0, new_size - old_size);
        buffer->data = resized;
        buffer->capacity_elements = num_elements;
    }

    buffer->num_elements = num_elements;
    pthread_mutex_unlock(&backend->mutex);
    return VA_STATUS_SUCCESS;
}

static VAStatus backend_map_buffer(VADriverContextP context,
                                   VABufferID buffer_id,
                                   void **mapped)
{
    struct venus_backend *backend =
        venus_backend_from_context(context);
    struct venus_buffer *buffer;

    if (!backend || !mapped)
        return VA_STATUS_ERROR_INVALID_PARAMETER;

    pthread_mutex_lock(&backend->mutex);
    buffer = venus_backend_find_buffer(backend, buffer_id);
    if (!buffer) {
        pthread_mutex_unlock(&backend->mutex);
        return VA_STATUS_ERROR_INVALID_BUFFER;
    }

    *mapped = buffer->type == VAEncCodedBufferType
                  ? (void *)&buffer->coded_segment
                  : (void *)buffer->data;
    pthread_mutex_unlock(&backend->mutex);
    return VA_STATUS_SUCCESS;
}

static VAStatus backend_unmap_buffer(VADriverContextP context,
                                     VABufferID buffer_id)
{
    struct venus_backend *backend =
        venus_backend_from_context(context);
    VAStatus status;

    if (!backend)
        return VA_STATUS_ERROR_INVALID_CONTEXT;

    pthread_mutex_lock(&backend->mutex);
    status = venus_backend_find_buffer(backend, buffer_id)
                 ? VA_STATUS_SUCCESS
                 : VA_STATUS_ERROR_INVALID_BUFFER;
    pthread_mutex_unlock(&backend->mutex);
    return status;
}

static void release_buffer_handle_locked(struct venus_backend *backend,
                                         VABufferID buffer_id);

static bool buffer_used_by_image(struct venus_backend *backend,
                                 VABufferID buffer_id)
{
    unsigned int index;

    for (index = 0; index < VENUS_MAX_IMAGES; index++) {
        if (backend->images[index].used &&
            backend->images[index].buffer_id == buffer_id)
            return true;
    }

    return false;
}

static VAStatus backend_destroy_buffer(VADriverContextP context,
                                       VABufferID buffer_id)
{
    struct venus_backend *backend =
        venus_backend_from_context(context);
    struct venus_buffer *buffer;

    if (!backend)
        return VA_STATUS_ERROR_INVALID_CONTEXT;

    pthread_mutex_lock(&backend->mutex);
    buffer = venus_backend_find_buffer(backend, buffer_id);
    if (!buffer || buffer_used_by_image(backend, buffer_id)) {
        pthread_mutex_unlock(&backend->mutex);
        return VA_STATUS_ERROR_INVALID_BUFFER;
    }

    if (buffer->type == VAEncCodedBufferType &&
        buffer->source_surface_id != VA_INVALID_ID) {
        struct venus_surface *surface = venus_backend_find_surface(
            backend, buffer->source_surface_id);

        if (surface && surface->coded_buffer_id == buffer->id) {
            surface->encode_pending = false;
            surface->coded_buffer_id = VA_INVALID_ID;
        }
    }

    /* Keep the fd table in step with the buffer table: buffer IDs are reused
     * by the next allocate_buffer(), so a stale entry would hand a client an
     * fd for the wrong buffer - or close one it is still using.
     */
    release_buffer_handle_locked(backend, buffer_id);

    venus_backend_free_buffer(buffer);
    pthread_mutex_unlock(&backend->mutex);
    return VA_STATUS_SUCCESS;
}

static VAStatus backend_buffer_info(
    VADriverContextP context, VABufferID buffer_id,
    VABufferType *type, unsigned int *size,
    unsigned int *num_elements)
{
    struct venus_backend *backend =
        venus_backend_from_context(context);
    struct venus_buffer *buffer;

    if (!backend || !type || !size || !num_elements)
        return VA_STATUS_ERROR_INVALID_PARAMETER;

    pthread_mutex_lock(&backend->mutex);
    buffer = venus_backend_find_buffer(backend, buffer_id);
    if (!buffer || buffer->element_size > UINT_MAX ||
        buffer->num_elements > UINT_MAX) {
        pthread_mutex_unlock(&backend->mutex);
        return VA_STATUS_ERROR_INVALID_BUFFER;
    }

    *type = buffer->type;
    *size = (unsigned int)buffer->element_size;
    *num_elements = (unsigned int)buffer->num_elements;
    pthread_mutex_unlock(&backend->mutex);
    return VA_STATUS_SUCCESS;
}

static VAImageFormat nv12_image_format(void)
{
    return (VAImageFormat) {
        .fourcc = VA_FOURCC_NV12,
        .byte_order = VA_LSB_FIRST,
        .bits_per_pixel = 12,
        .depth = 12,
    };
}

static VAStatus backend_query_image_formats(
    VADriverContextP context, VAImageFormat *formats,
    int *num_formats)
{
    struct venus_backend *backend =
        venus_backend_from_context(context);

    if (!backend || !num_formats)
        return VA_STATUS_ERROR_INVALID_PARAMETER;

    if (formats)
        formats[0] = nv12_image_format();
    *num_formats = 1;
    return VA_STATUS_SUCCESS;
}

static VAStatus create_image_locked(
    struct venus_backend *backend, unsigned int width,
    unsigned int height, uint8_t *external_data,
    size_t external_size, VASurfaceID surface_id,
    VAImage *result)
{
    struct venus_buffer *buffer = NULL;
    struct venus_image *image = NULL;
    size_t data_size;
    unsigned int index;

    if (!result || nv12_size(width, height, &data_size) < 0)
        return VA_STATUS_ERROR_INVALID_PARAMETER;
    if (external_data && external_size < data_size)
        return VA_STATUS_ERROR_NOT_ENOUGH_BUFFER;

    buffer = allocate_buffer(
        backend, VA_INVALID_ID, VAImageBufferType,
        external_data ? external_size : data_size, 1,
        external_data, !external_data);
    if (!buffer)
        return VA_STATUS_ERROR_ALLOCATION_FAILED;

    for (index = 0; index < VENUS_MAX_IMAGES; index++) {
        if (!backend->images[index].used) {
            image = &backend->images[index];
            break;
        }
    }
    if (!image) {
        venus_backend_free_buffer(buffer);
        return VA_STATUS_ERROR_MAX_NUM_EXCEEDED;
    }

    *image = (struct venus_image) {
        .used = true,
        .id = VENUS_IMAGE_BASE | (index + 1),
        .buffer_id = buffer->id,
        .surface_id = surface_id,
    };

    memset(result, 0, sizeof(*result));
    result->image_id = image->id;
    result->format = nv12_image_format();
    result->buf = buffer->id;
    result->width = (uint16_t)width;
    result->height = (uint16_t)height;
    result->data_size = (uint32_t)data_size;
    result->num_planes = 2;
    result->pitches[0] = width;
    result->pitches[1] = width;
    result->offsets[0] = 0;
    result->offsets[1] = width * height;
    return VA_STATUS_SUCCESS;
}

static VAStatus backend_create_image(
    VADriverContextP context, VAImageFormat *format,
    int width, int height, VAImage *image)
{
    struct venus_backend *backend =
        venus_backend_from_context(context);
    VAStatus status;

    if (!backend || !format || width < 0 || height < 0 ||
        format->fourcc != VA_FOURCC_NV12)
        return VA_STATUS_ERROR_INVALID_IMAGE_FORMAT;

    pthread_mutex_lock(&backend->mutex);
    status = create_image_locked(
        backend, (unsigned int)width, (unsigned int)height,
        NULL, 0, VA_INVALID_ID, image);
    pthread_mutex_unlock(&backend->mutex);
    return status;
}

static VAStatus backend_derive_image(VADriverContextP context,
                                     VASurfaceID surface_id,
                                     VAImage *image)
{
    struct venus_backend *backend =
        venus_backend_from_context(context);
    struct venus_surface *surface;
    VAStatus status;

    if (!backend || !image)
        return VA_STATUS_ERROR_INVALID_PARAMETER;

    pthread_mutex_lock(&backend->mutex);
    surface = venus_backend_find_surface(backend, surface_id);
    if (!surface) {
        pthread_mutex_unlock(&backend->mutex);
        return VA_STATUS_ERROR_INVALID_SURFACE;
    }
    if (!surface->ready) {
        pthread_mutex_unlock(&backend->mutex);
        return VA_STATUS_ERROR_SURFACE_BUSY;
    }

    status = create_image_locked(
        backend, surface->width, surface->height,
        surface->data, surface->capacity, surface->id, image);
    if (status == VA_STATUS_SUCCESS)
        venus_backend_log(backend,
                          "derive-image surface=0x%x image=0x%x bytes=%u",
                          surface->id, image->image_id, image->data_size);
    pthread_mutex_unlock(&backend->mutex);
    return status;
}

static VAStatus backend_destroy_image(VADriverContextP context,
                                      VAImageID image_id)
{
    struct venus_backend *backend =
        venus_backend_from_context(context);
    struct venus_image *image;
    struct venus_buffer *buffer;

    if (!backend)
        return VA_STATUS_ERROR_INVALID_CONTEXT;

    pthread_mutex_lock(&backend->mutex);
    image = venus_backend_find_image(backend, image_id);
    if (!image) {
        pthread_mutex_unlock(&backend->mutex);
        return VA_STATUS_ERROR_INVALID_IMAGE;
    }

    buffer = venus_backend_find_buffer(backend, image->buffer_id);
    release_buffer_handle_locked(backend, image->buffer_id);
    venus_backend_free_buffer(buffer);
    memset(image, 0, sizeof(*image));
    pthread_mutex_unlock(&backend->mutex);
    return VA_STATUS_SUCCESS;
}

static VAStatus backend_get_image(
    VADriverContextP context, VASurfaceID surface_id,
    int x, int y, unsigned int width, unsigned int height,
    VAImageID image_id)
{
    struct venus_backend *backend =
        venus_backend_from_context(context);
    struct venus_surface *surface;
    struct venus_image *image;
    struct venus_buffer *buffer;
    size_t copy_size;

    if (!backend)
        return VA_STATUS_ERROR_INVALID_CONTEXT;

    pthread_mutex_lock(&backend->mutex);
    surface = venus_backend_find_surface(backend, surface_id);
    image = venus_backend_find_image(backend, image_id);
    buffer = image
                 ? venus_backend_find_buffer(backend, image->buffer_id)
                 : NULL;
    if (!surface || !image || !buffer) {
        pthread_mutex_unlock(&backend->mutex);
        return !surface ? VA_STATUS_ERROR_INVALID_SURFACE
                        : VA_STATUS_ERROR_INVALID_IMAGE;
    }
    if (!surface->ready) {
        pthread_mutex_unlock(&backend->mutex);
        return VA_STATUS_ERROR_SURFACE_BUSY;
    }
    if (x != 0 || y != 0 || width != surface->width ||
        height != surface->height ||
        nv12_size(width, height, &copy_size) < 0 ||
        copy_size > surface->data_size ||
        copy_size > buffer->element_size * buffer->num_elements) {
        pthread_mutex_unlock(&backend->mutex);
        return VA_STATUS_ERROR_INVALID_PARAMETER;
    }

    memcpy(buffer->data, surface->data, copy_size);
    pthread_mutex_unlock(&backend->mutex);
    return VA_STATUS_SUCCESS;
}

static VAStatus backend_put_image(
    VADriverContextP context, VASurfaceID surface_id,
    VAImageID image_id, int src_x, int src_y,
    unsigned int src_width, unsigned int src_height,
    int dst_x, int dst_y, unsigned int dst_width,
    unsigned int dst_height)
{
    struct venus_backend *backend =
        venus_backend_from_context(context);
    struct venus_surface *surface;
    struct venus_image *image;
    struct venus_buffer *buffer;
    size_t copy_size;

    if (!backend)
        return VA_STATUS_ERROR_INVALID_CONTEXT;

    pthread_mutex_lock(&backend->mutex);
    surface = venus_backend_find_surface(backend, surface_id);
    image = venus_backend_find_image(backend, image_id);
    buffer = image
                 ? venus_backend_find_buffer(backend, image->buffer_id)
                 : NULL;
    if (!surface || !image || !buffer) {
        pthread_mutex_unlock(&backend->mutex);
        return !surface ? VA_STATUS_ERROR_INVALID_SURFACE
                        : VA_STATUS_ERROR_INVALID_IMAGE;
    }
    if (surface->encode_pending) {
        pthread_mutex_unlock(&backend->mutex);
        return VA_STATUS_ERROR_SURFACE_BUSY;
    }
    if (src_x != 0 || src_y != 0 || dst_x != 0 || dst_y != 0 ||
        src_width != surface->width ||
        src_height != surface->height ||
        dst_width != surface->width ||
        dst_height != surface->height ||
        nv12_size(surface->width, surface->height, &copy_size) < 0 ||
        copy_size > buffer->element_size * buffer->num_elements ||
        copy_size > surface->capacity) {
        pthread_mutex_unlock(&backend->mutex);
        return VA_STATUS_ERROR_INVALID_PARAMETER;
    }

    memcpy(surface->data, buffer->data, copy_size);
    surface->data_size = copy_size;
    surface->ready = true;
    pthread_mutex_unlock(&backend->mutex);
    return VA_STATUS_SUCCESS;
}

static VAStatus backend_query_surface_attributes(
    VADriverContextP context, VAConfigID config_id,
    VASurfaceAttrib *attributes, unsigned int *num_attributes)
{
    struct venus_backend *backend =
        venus_backend_from_context(context);
    struct venus_config *config;
    VASurfaceAttrib values[6];
    unsigned int required =
        (unsigned int)(sizeof(values) / sizeof(values[0]));

    if (!backend || !num_attributes)
        return VA_STATUS_ERROR_INVALID_PARAMETER;

    pthread_mutex_lock(&backend->mutex);
    config = venus_backend_find_config(backend, config_id);
    if (!config) {
        pthread_mutex_unlock(&backend->mutex);
        return VA_STATUS_ERROR_INVALID_CONFIG;
    }

    values[0] = (VASurfaceAttrib) {
        .type = VASurfaceAttribPixelFormat,
        .flags = VA_SURFACE_ATTRIB_GETTABLE |
                 VA_SURFACE_ATTRIB_SETTABLE,
        .value = {
            .type = VAGenericValueTypeInteger,
            .value.i = VA_FOURCC_NV12,
        },
    };
    values[1] = (VASurfaceAttrib) {
        .type = VASurfaceAttribMemoryType,
        .flags = VA_SURFACE_ATTRIB_GETTABLE |
                 VA_SURFACE_ATTRIB_SETTABLE,
        .value = {
            .type = VAGenericValueTypeInteger,
            .value.i = VA_SURFACE_ATTRIB_MEM_TYPE_VA |
                       VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2,
        },
    };
    values[2] = (VASurfaceAttrib) {
        .type = VASurfaceAttribMinWidth,
        .flags = VA_SURFACE_ATTRIB_GETTABLE,
        .value = {
            .type = VAGenericValueTypeInteger,
            .value.i = VENUS_MIN_SURFACE_WIDTH,
        },
    };
    values[3] = (VASurfaceAttrib) {
        .type = VASurfaceAttribMinHeight,
        .flags = VA_SURFACE_ATTRIB_GETTABLE,
        .value = {
            .type = VAGenericValueTypeInteger,
            .value.i = VENUS_MIN_SURFACE_HEIGHT,
        },
    };
    values[4] = (VASurfaceAttrib) {
        .type = VASurfaceAttribMaxWidth,
        .flags = VA_SURFACE_ATTRIB_GETTABLE,
        .value = {
            .type = VAGenericValueTypeInteger,
            .value.i = VENUS_MAX_WIDTH,
        },
    };
    values[5] = (VASurfaceAttrib) {
        .type = VASurfaceAttribMaxHeight,
        .flags = VA_SURFACE_ATTRIB_GETTABLE,
        .value = {
            .type = VAGenericValueTypeInteger,
            .value.i = VENUS_MAX_HEIGHT,
        },
    };

    if (!attributes) {
        *num_attributes = required;
        pthread_mutex_unlock(&backend->mutex);
        return VA_STATUS_SUCCESS;
    }
    /* *num_attributes is out-only per va.h (see backend.c); the caller
     * sizes the array to vaMaxNumSurfaceAttributes(). */
    memcpy(attributes, values, sizeof(values));
    *num_attributes = required;
    pthread_mutex_unlock(&backend->mutex);
    return VA_STATUS_SUCCESS;
}

void venus_objects_destroy_all(struct venus_backend *backend)
{
    unsigned int index;

    for (index = 0; index < VENUS_MAX_BUFFER_HANDLES; index++) {
        if (backend->handles[index].fd >= 0 &&
            backend->handles[index].buffer_id)
            close(backend->handles[index].fd);
        memset(&backend->handles[index], 0,
               sizeof(backend->handles[index]));
    }
    for (index = 0; index < VENUS_MAX_IMAGES; index++)
        memset(&backend->images[index], 0,
               sizeof(backend->images[index]));
    for (index = 0; index < VENUS_MAX_BUFFERS; index++)
        venus_backend_free_buffer(&backend->buffers[index]);
    for (index = 0; index < VENUS_MAX_SURFACES; index++) {
        surface_memory_release(backend->surfaces[index].data,
                               backend->surfaces[index].dmabuf_fd,
                               backend->surfaces[index].capacity);
        memset(&backend->surfaces[index], 0,
               sizeof(backend->surfaces[index]));
    }
    memset(backend->configs, 0, sizeof(backend->configs));
}

/*
 * Zero-copy hand-off.  Both mpv (libavutil) and VLC resolve
 * vaExportSurfaceHandle and will not use hardware decoding without it; with it
 * they can import the decoded surface as an EGLImage and stay on the GPU.
 *
 * A linearly-mapped DMA-BUF heap buffer holds NV12 as one object.  The
 * application owns the returned fd and closes it.
 *
 * The layer layout has to follow what the caller asked for, and the default
 * matters: VLC's GL interop passes neither VA_EXPORT_SURFACE_SEPARATE_LAYERS
 * nor VA_EXPORT_SURFACE_COMPOSED_LAYERS, and then rejects any layer that holds
 * more than one plane.  Answering with a single composed NV12 layer made it
 * drop every frame into a texture that was never filled - a flat green picture.
 * Separate layers are therefore the default; only an explicit
 * VA_EXPORT_SURFACE_COMPOSED_LAYERS request gets the composed layer.
 */
static VAStatus backend_export_surface_handle(
    VADriverContextP context, VASurfaceID surface_id, uint32_t memory_type,
    uint32_t flags, void *descriptor)
{
    struct venus_backend *backend =
        venus_backend_from_context(context);
    VADRMPRIMESurfaceDescriptor *exported = descriptor;
    struct venus_surface *surface;
    bool composed;
    uint32_t luma_size;
    int fd;

    if (!backend || !descriptor)
        return VA_STATUS_ERROR_INVALID_PARAMETER;
    if (memory_type != VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2)
        return VA_STATUS_ERROR_UNSUPPORTED_MEMORY_TYPE;
    if (flags & ~(uint32_t)(VA_EXPORT_SURFACE_READ_ONLY |
                            VA_EXPORT_SURFACE_WRITE_ONLY |
                            VA_EXPORT_SURFACE_READ_WRITE |
                            VA_EXPORT_SURFACE_SEPARATE_LAYERS |
                            VA_EXPORT_SURFACE_COMPOSED_LAYERS))
        return VA_STATUS_ERROR_INVALID_PARAMETER;
    composed = (flags & VA_EXPORT_SURFACE_COMPOSED_LAYERS) != 0;

    pthread_mutex_lock(&backend->mutex);
    surface = venus_backend_find_surface(backend, surface_id);
    if (!surface || surface->dmabuf_fd < 0) {
        pthread_mutex_unlock(&backend->mutex);
        return surface ? VA_STATUS_ERROR_UNSUPPORTED_MEMORY_TYPE
                       : VA_STATUS_ERROR_INVALID_SURFACE;
    }

    fd = fcntl(surface->dmabuf_fd, F_DUPFD_CLOEXEC, 0);
    if (fd < 0) {
        pthread_mutex_unlock(&backend->mutex);
        return VA_STATUS_ERROR_OPERATION_FAILED;
    }

    luma_size = surface->width * surface->height;
    memset(exported, 0, sizeof(*exported));
    exported->fourcc = VA_FOURCC_NV12;
    exported->width = surface->width;
    exported->height = surface->height;
    exported->num_objects = 1;
    /* Both layouts live in this single object, so every layer refers to
     * object 0 - object_index[] is zeroed by the memset above. */
    exported->objects[0].fd = fd;
    exported->objects[0].size = (uint32_t)surface->capacity;
    exported->objects[0].drm_format_modifier = VENUS_DRM_FORMAT_MOD_LINEAR;

    if (composed) {
        /* Luma and interleaved chroma as the two planes of one NV12 layer. */
        exported->num_layers = 1;
        exported->layers[0].drm_format = VENUS_DRM_FORMAT_NV12;
        exported->layers[0].num_planes = 2;
        exported->layers[0].offset[0] = 0;
        exported->layers[0].offset[1] = luma_size;
        exported->layers[0].pitch[0] = surface->width;
        exported->layers[0].pitch[1] = surface->width;
    } else {
        /* One layer per plane, which is the layout the pre-1.1 API callers
         * expect: the layers carry the DRM plane formats. */
        exported->num_layers = 2;
        exported->layers[0].drm_format = VENUS_DRM_FORMAT_R8;
        exported->layers[0].num_planes = 1;
        exported->layers[0].offset[0] = 0;
        exported->layers[0].pitch[0] = surface->width;
        exported->layers[1].drm_format = VENUS_DRM_FORMAT_GR88;
        exported->layers[1].num_planes = 1;
        exported->layers[1].offset[0] = luma_size;
        exported->layers[1].pitch[0] = surface->width;
    }
    pthread_mutex_unlock(&backend->mutex);

    venus_backend_log(backend,
                      "export-surface id=0x%x fd=%d bytes=%zu %ux%u %s",
                      surface_id, fd, surface->capacity, surface->width,
                      surface->height,
                      composed ? "composed" : "separate-layers");
    return VA_STATUS_SUCCESS;
}

/*
 * Legacy DRM prime hand-off.
 *
 * VLC's GL interop (glconv_vaapi_wl) imports surfaces through the pre-1.1 API:
 * it derives a VAImage for the surface and then asks for the surface's DMA-BUF
 * with vaAcquireBufferHandle(image.buf, mem_type = 0).  Only
 * vaExportSurfaceHandle() used to be implemented, so the stub in va_stubs.c
 * answered VA_STATUS_ERROR_INVALID_BUFFER and VLC abandoned hardware decoding
 * ("video output creation failed" -> 4K software decode -> frozen picture).
 *
 * The handle is a dup() of the surface's DMA-BUF fd: the application can import
 * it with EGL and close it whenever it likes without disturbing the surface,
 * and the surface keeps its own reference for decoding.
 *
 * The spec makes this a synchronisation point, but clients only get here after
 * vaSyncSurface() on the parent surface - the derived image is useless
 * otherwise - and the decode path has already written the frame into the
 * surface by then.
 */
static struct venus_buffer_handle *acquired_handle_locked(
    struct venus_backend *backend, VABufferID buffer_id)
{
    unsigned int index;

    if (!buffer_id)
        return NULL;

    for (index = 0; index < VENUS_MAX_BUFFER_HANDLES; index++) {
        if (backend->handles[index].buffer_id == buffer_id)
            return &backend->handles[index];
    }

    return NULL;
}

static void release_buffer_handle_locked(struct venus_backend *backend,
                                         VABufferID buffer_id)
{
    struct venus_buffer_handle *handle =
        acquired_handle_locked(backend, buffer_id);

    if (!handle)
        return;

    if (handle->fd >= 0)
        close(handle->fd);
    *handle = (struct venus_buffer_handle) { 0 };
}

/* The surface behind a buffer, if that buffer is an image's backing store.
 * Clients hand us image.buf, not the surface, so the image table is the only
 * link between the two.
 */
static struct venus_surface *image_surface_for_buffer_locked(
    struct venus_backend *backend, VABufferID buffer_id)
{
    unsigned int index;

    for (index = 0; index < VENUS_MAX_IMAGES; index++) {
        if (backend->images[index].used &&
            backend->images[index].buffer_id == buffer_id)
            return venus_backend_find_surface(
                backend, backend->images[index].surface_id);
    }

    return NULL;
}

static VAStatus backend_acquire_buffer_handle(VADriverContextP context,
                                              VABufferID buffer_id,
                                              VABufferInfo *info)
{
    struct venus_backend *backend =
        venus_backend_from_context(context);
    struct venus_buffer_handle *handle;
    struct venus_surface *surface;
    unsigned long exported_handle;
    size_t size;
    int fd;
    unsigned int index;

    if (!backend || !info)
        return VA_STATUS_ERROR_INVALID_PARAMETER;
    /* mem_type is a bit mask of the memory types the caller accepts on input
     * and the one that was used on output; 0 means "driver's choice". */
    if (info->mem_type != 0 &&
        info->mem_type != VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME)
        return VA_STATUS_ERROR_UNSUPPORTED_MEMORY_TYPE;

    pthread_mutex_lock(&backend->mutex);
    if (!venus_backend_find_buffer(backend, buffer_id)) {
        pthread_mutex_unlock(&backend->mutex);
        return VA_STATUS_ERROR_INVALID_BUFFER;
    }

    surface = image_surface_for_buffer_locked(backend, buffer_id);
    if (!surface) {
        /* Plain buffers are CPU memory with no DMA-BUF behind them. */
        pthread_mutex_unlock(&backend->mutex);
        return VA_STATUS_ERROR_UNSUPPORTED_BUFFERTYPE;
    }
    if (surface->dmabuf_fd < 0) {
        /* No accessible DMA-BUF heap: surface_memory_allocate() fell back to
         * plain memory, so nothing can be exported. */
        pthread_mutex_unlock(&backend->mutex);
        return VA_STATUS_ERROR_UNSUPPORTED_MEMORY_TYPE;
    }

    handle = acquired_handle_locked(backend, buffer_id);
    if (!handle) {
        for (index = 0; index < VENUS_MAX_BUFFER_HANDLES; index++) {
            if (!backend->handles[index].buffer_id) {
                handle = &backend->handles[index];
                break;
            }
        }
    }
    if (!handle) {
        pthread_mutex_unlock(&backend->mutex);
        return VA_STATUS_ERROR_MAX_NUM_EXCEEDED;
    }

    if (handle->buffer_id != buffer_id) {
        fd = fcntl(surface->dmabuf_fd, F_DUPFD_CLOEXEC, 0);
        if (fd < 0) {
            pthread_mutex_unlock(&backend->mutex);
            return VA_STATUS_ERROR_OPERATION_FAILED;
        }
        handle->buffer_id = buffer_id;
        handle->fd = fd;
    } else {
        /* Re-acquiring an already held handle: hand out the same fd, which
         * stays valid until vaReleaseBufferHandle(). */
        fd = handle->fd;
    }

    exported_handle = (unsigned long)fd;
    size = surface->capacity;
    info->mem_type = VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME;
    info->handle = exported_handle;
    info->mem_size = size;
    pthread_mutex_unlock(&backend->mutex);

    venus_backend_log(backend,
                      "acquire-buffer-handle buffer=0x%x fd=%d bytes=%zu",
                      buffer_id, fd, size);
    return VA_STATUS_SUCCESS;
}

static VAStatus backend_release_buffer_handle(VADriverContextP context,
                                              VABufferID buffer_id)
{
    struct venus_backend *backend =
        venus_backend_from_context(context);

    if (!backend)
        return VA_STATUS_ERROR_INVALID_CONTEXT;

    pthread_mutex_lock(&backend->mutex);
    /* Idempotent on purpose: clients such as VLC call this unconditionally,
     * including straight after a failed acquire. */
    release_buffer_handle_locked(backend, buffer_id);
    pthread_mutex_unlock(&backend->mutex);
    return VA_STATUS_SUCCESS;
}

void venus_objects_fill_vtable(struct VADriverVTable *vtable)
{
    vtable->vaCreateSurfaces = backend_create_surfaces;
    vtable->vaCreateSurfaces2 = backend_create_surfaces2;
    vtable->vaDestroySurfaces = backend_destroy_surfaces;
    vtable->vaCreateBuffer = backend_create_buffer;
    vtable->vaBufferSetNumElements = backend_set_num_elements;
    vtable->vaMapBuffer = backend_map_buffer;
    vtable->vaUnmapBuffer = backend_unmap_buffer;
    vtable->vaDestroyBuffer = backend_destroy_buffer;
    vtable->vaBufferInfo = backend_buffer_info;
    vtable->vaQueryImageFormats = backend_query_image_formats;
    vtable->vaCreateImage = backend_create_image;
    vtable->vaDeriveImage = backend_derive_image;
    vtable->vaDestroyImage = backend_destroy_image;
    vtable->vaGetImage = backend_get_image;
    vtable->vaPutImage = backend_put_image;
    vtable->vaQuerySurfaceAttributes =
        backend_query_surface_attributes;
    vtable->vaExportSurfaceHandle = backend_export_surface_handle;
    vtable->vaAcquireBufferHandle = backend_acquire_buffer_handle;
    vtable->vaReleaseBufferHandle = backend_release_buffer_handle;
}
