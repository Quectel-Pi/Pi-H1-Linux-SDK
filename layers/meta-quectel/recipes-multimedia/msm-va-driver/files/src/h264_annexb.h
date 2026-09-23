// SPDX-License-Identifier: MIT
#ifndef VENUS_H264_ANNEXB_H
#define VENUS_H264_ANNEXB_H

#include <stddef.h>
#include <stdint.h>
#include <va/va.h>

/*
 * VA-API puts the same three members first in every codec's slice parameter
 * buffer (it calls this the codec-independent slice parameter buffer base),
 * so one description walks H.264, HEVC and VP9 slices.
 */
struct venus_slice_parameters {
    uint32_t slice_data_size;
    uint32_t slice_data_offset;
    uint32_t slice_data_flag;
};

/*
 * One VASliceDataBufferType and the slice parameters that go with it.  A
 * batch without parameters is a whole buffer that is a frame on its own,
 * which is what VP9 sends.
 */
struct venus_slice_batch {
    const struct venus_slice_parameters *parameters;
    size_t num_parameters;
    const uint8_t *data;
    size_t data_size;
};

struct venus_annexb_writer {
    uint8_t *data;
    size_t capacity;
    size_t length;
};

int venus_annexb_write_bytes(struct venus_annexb_writer *writer,
                             const void *data, size_t size);

/* Appends one NAL unit, adding a start code when the bytes lack one. */
int venus_annexb_write_slice(struct venus_annexb_writer *writer,
                             const uint8_t *slice, size_t size);

/* Appends a NAL unit built from header + payload, escaping emulation
 * prevention bytes in the payload.
 */
int venus_annexb_write_nal(struct venus_annexb_writer *writer,
                           const uint8_t *header, size_t header_size,
                           const uint8_t *rbsp, size_t rbsp_size);

/* Appends the slices a slice parameter buffer describes out of data. */
int venus_annexb_write_slices(struct venus_annexb_writer *writer,
                              const struct venus_slice_parameters *parameters,
                              size_t num_parameters, const uint8_t *data,
                              size_t data_size);

int venus_annexb_write_batch(struct venus_annexb_writer *writer,
                             const struct venus_slice_batch *batch);

int venus_h264_build_access_unit(
    VAProfile profile, const VAPictureParameterBufferH264 *picture,
    const struct venus_slice_batch *batches, size_t num_batches,
    uint8_t *output, size_t output_capacity, size_t *output_size);

#endif
