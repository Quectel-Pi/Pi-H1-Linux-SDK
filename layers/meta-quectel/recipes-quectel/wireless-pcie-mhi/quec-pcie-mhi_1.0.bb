SUMMARY = "Quectel PCIe MHI wireless driver"
LICENSE = "CLOSED"

inherit module

SRC_URI = "file://pcie_mhi"
S = "${WORKDIR}/pcie_mhi"

EXTRA_OEMAKE += "KDIR=${STAGING_KERNEL_DIR}"

RPROVIDES:${PN} += "kernel-module-pcie-mhi"
KERNEL_MODULE_AUTOLOAD += "pcie_mhi"

RM_WORK_EXCLUDE += "${PN}"
