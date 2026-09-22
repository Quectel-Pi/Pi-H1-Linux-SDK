DESCRIPTION = "EFI System Partition Image to boot Qualcomm boards"
LICENSE = "BSD-3-Clause-Clear"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/BSD-3-Clause-Clear;md5=7a434440b651f4a472ca93716d01033a"

COMPATIBLE_HOST = '(x86_64.*|arm.*|aarch64.*)-(linux.*)'

PACKAGE_INSTALL = " \
    linux-qcom-uki \
    systemd-boot \
    systemd-bootconf \
"

KERNELDEPMODDEPEND = ""
KERNEL_DEPLOY_DEPEND = ""

inherit image

IMAGE_FSTYPES = "vfat"
IMAGE_FSTYPES_DEBUGFS = ""

# efi partition is 524288KB (512MiB). Pin the image just below it: mkfs.vfat
# auto-selects FAT32 at >=512MiB, and with 64KiB clusters (EXTRA_IMAGECMD in
# meta-quectel) FAT32 needs >=65525 clusters -> do_image_vfat fails.
IMAGE_ROOTFS_SIZE = "512000"

LINGUAS_INSTALL = ""
