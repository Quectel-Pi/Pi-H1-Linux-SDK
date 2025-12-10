SUMMARY = "yt6801 driver"
LICENSE = "CLOSED"

inherit module

FILESPATH:prepend =  "${WORKSPACE}/sources/quectel-src:"
SRC_URI = "file://ethernet-drivers/yt6801"
S = "${WORKDIR}/ethernet-drivers/yt6801"

EXTRA_OEMAKE += "KDIR=${STAGING_KERNEL_DIR}"

RPROVIDES_${PN} += "kernel-module-yt6801"
KERNEL_MODULE_AUTOLOAD += "yt6801"

RM_WORK_EXCLUDE += "${PN}"
