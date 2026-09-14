SUMMARY = "Quectel UFS size proc module"
LICENSE = "CLOSED"

inherit module

SRC_URI = "file://quec-ufs-size"
S = "${WORKDIR}/quec-ufs-size"

EXTRA_OEMAKE += "KDIR=${STAGING_KERNEL_DIR}"

RPROVIDES:${PN} += "kernel-module-quec-ufs-size"
KERNEL_MODULE_AUTOLOAD += "quec_ufs_size"

RM_WORK_EXCLUDE += "${PN}"
