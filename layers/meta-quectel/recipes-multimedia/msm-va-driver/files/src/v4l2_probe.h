// SPDX-License-Identifier: MIT
#ifndef VENUS_V4L2_PROBE_H
#define VENUS_V4L2_PROBE_H

#include "venus/capabilities.h"

struct v4l2_capability;

/* Recognised stateful M2M video drivers. Upstream Venus reports
 * "qcom-venus"; Qualcomm's downstream/out-of-tree driver (msm_vidc,
 * shipped as iris_vpu.ko on QCM6490/QCS6490 boards) reports
 * "msm_vidc_driver" while exposing the very same V4L2 M2M interface.
 */
bool venus_v4l2_driver_supported(const struct v4l2_capability *cap);

int venus_v4l2_probe(struct venus_capabilities *caps);
int venus_v4l2_probe_prefix(struct venus_capabilities *caps,
                            const char *device_prefix, unsigned int limit);

#endif
