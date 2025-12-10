SUMMARY = "r8168 driver"
LICENSE = "CLOSED"

inherit module

FILESPATH:prepend =  "${WORKSPACE}/sources/quectel-src:"
SRC_URI = "file://ethernet-drivers/r8168"
S = "${WORKDIR}/ethernet-drivers/r8168"

MAKE_TARGETS = "modules"
EXTRA_OEMAKE += "KERNELDIR=${STAGING_KERNEL_DIR}"

RPROVIDES_${PN} += "kernel-module-r8168"
KERNEL_MODULE_AUTOLOAD += "r8168"

RM_WORK_EXCLUDE += "${PN}"
