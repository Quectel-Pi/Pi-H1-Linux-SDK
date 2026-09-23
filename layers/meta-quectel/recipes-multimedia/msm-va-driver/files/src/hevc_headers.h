// SPDX-License-Identifier: MIT
#ifndef VENUS_HEVC_HEADERS_H
#define VENUS_HEVC_HEADERS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <va/va.h>
#include <va/va_dec_hevc.h>

#include "h264_annexb.h"

/* Enough for a VPS, an SPS and a PPS of a normal stream, escaping
 * included.
 */
#define VENUS_HEVC_HEADERS_MAX 256

/*
 * A VA-API HEVC client hands the driver parsed fields, never the raw
 * VPS/SPS/PPS NAL units, while the firmware this driver talks to parses a
 * bitstream.  The parameter sets are therefore built back from
 * VAPictureParameterBufferHEVC, which mirrors most of what an SPS and a PPS
 * carry.  Both are rebuilt with the picture set identifiers a normal encoder
 * uses (0), and the coded size is cropped down to the size the client asked
 * for so the decoder reports the same visible picture.
 */
struct venus_hevc_sequence {
    const VAPictureParameterBufferHEVC *picture;
    uint32_t coded_width;
    uint32_t coded_height;
    uint32_t visible_width;
    uint32_t visible_height;
};

/*
 * Picture parameter set id the first slice of a picture uses.  A first slice
 * carries it a fixed number of bits in; anything else (a picture that starts
 * with a dependent slice, or garbage) reports 0, which is what encoders use.
 */
unsigned int venus_hevc_pps_id(const uint8_t *nal, size_t size, bool rap_pic);

/*
 * Appends VPS, SPS and PPS for the picture, with start codes.
 * Returns -ENOTSUP for streams that need parameter set contents this
 * interface does not carry (short term reference picture sets in the SPS,
 * long term reference pictures).
 */
int venus_hevc_write_parameter_sets(struct venus_annexb_writer *writer,
                                    const struct venus_hevc_sequence *sequence,
                                    unsigned int pps_id);

#endif
