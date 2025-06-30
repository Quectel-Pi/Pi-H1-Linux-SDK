SUMMARY = "yt6801 driver"
LICENSE = "CLOSED"

inherit module

SRC_URI = "file://yt6801.tgz"
S = "${WORKDIR}/yt6801"

EXTRA_OEMAKE += "KDIR=${STAGING_KERNEL_DIR}"

RPROVIDES_${PN} += "kernel-module-yt6801"
KERNEL_MODULE_AUTOLOAD += "yt6801"

RM_WORK_EXCLUDE += "${PN}"
