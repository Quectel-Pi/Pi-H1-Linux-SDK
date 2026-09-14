SUMMARY = "Quectel SPRD PCIe wireless driver"
LICENSE = "CLOSED"

inherit module

SRC_URI = "file://sprd_pcie"
S = "${WORKDIR}/sprd_pcie"

EXTRA_OEMAKE += "KDIR=${STAGING_KERNEL_DIR}"

RPROVIDES:${PN} += "kernel-module-sprd-pcie"
KERNEL_MODULE_AUTOLOAD += "sprd_pcie"

RM_WORK_EXCLUDE += "${PN}"
