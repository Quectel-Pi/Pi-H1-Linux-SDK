DESCRIPTION = "DTB Partition Image to boot Qualcomm boards"
LICENSE = "BSD-3-Clause-Clear"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/BSD-3-Clause-Clear;md5=7a434440b651f4a472ca93716d01033a"

COMPATIBLE_HOST = '(x86_64.*|arm.*|aarch64.*)-(linux.*)'

PACKAGE_INSTALL = " \
    linux-qcom-mergedtb-el2-combined \
"

# Rename el2.dtb as UEFI can only load dtb with name "combined-dtb.dtb"
ROOTFS_POSTPROCESS_COMMAND += 'rename_combined_dtb '

rename_combined_dtb () {
    if [ -e ${IMAGE_ROOTFS}/combined-dtb-el2.dtb ]; then
        mv ${IMAGE_ROOTFS}/combined-dtb-el2.dtb ${IMAGE_ROOTFS}/combined-dtb.dtb
    fi
}

KERNELDEPMODDEPEND = ""
KERNEL_DEPLOY_DEPEND = ""

inherit image

IMAGE_FSTYPES = "vfat"
IMAGE_FSTYPES_DEBUGFS = ""

ROOTFS_SIZE = "65536"

LINGUAS_INSTALL = ""
