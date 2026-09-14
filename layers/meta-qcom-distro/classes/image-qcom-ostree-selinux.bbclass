# Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause-Clear

ostree_selinux_set_labels() {
    POL_TYPE=$(sed -n -e "s&^SELINUXTYPE[[:space:]]*=[[:space:]]*\([0-9A-Za-z_]\+\)&\1&p" ${OTA_SYSROOT}/ostree/deploy/${OSTREE_OSNAME}/deploy/*/${sysconfdir}/selinux/config)
    if ! setfiles -m -r ${OTA_SYSROOT}/ostree/deploy/${OSTREE_OSNAME}/deploy/*/ ${OTA_SYSROOT}/ostree/deploy/${OSTREE_OSNAME}/deploy/*/${sysconfdir}/selinux/${POL_TYPE}/contexts/files/file_contexts ${OTA_SYSROOT}/ostree/deploy/${OSTREE_OSNAME}/deploy/*/ -e ${OTA_SYSROOT}/ostree/deploy/${OSTREE_OSNAME}/deploy/*/usr/etc
    then
        echo WARNING: Unable to set filesystem context, setfiles / restorecon must be run on the live image.
        touch ${OTA_SYSROOT}/.autorelabel
        exit 0
    fi
}

DEPENDS += "policycoreutils-native"

IMAGE_CMD:ota += " ostree_selinux_set_labels"

# Specify the task order
addtask ostree_selinux_set_labels after do_image_ota before do_image_ota_ext4
