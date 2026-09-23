// SPDX-License-Identifier: MIT
#ifndef VENUS_ANNEXB_SPLIT_H
#define VENUS_ANNEXB_SPLIT_H

#include <stddef.h>
#include <stdint.h>

typedef int (*venus_access_unit_callback)(const uint8_t *data, size_t size,
                                          void *opaque);

/* H.264 and HEVC both delimit NAL units with Annex-B start codes, but the
 * picture delimiter is a different NAL header layout, so the caller has to say
 * which codec the stream belongs to. VP9 has no start codes at all and cannot
 * be split here. */
enum venus_annexb_codec {
    VENUS_ANNEXB_H264,
    VENUS_ANNEXB_HEVC,
};

int venus_annexb_for_each_access_unit(const uint8_t *data, size_t size,
                                      enum venus_annexb_codec codec,
                                      venus_access_unit_callback callback,
                                      void *opaque, size_t *num_units);

#endif
