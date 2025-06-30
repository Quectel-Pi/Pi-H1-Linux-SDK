SUMMARY = "usb net qmi_wwan_q for Quectel modules"
LICENSE = "CLOSED"

inherit module

SRC_URI = "file://drmtest"
S = "${WORKDIR}/drmtest"

EXTRA_OEMAKE += "KDIR=${STAGING_KERNEL_DIR}"

RPROVIDES_${PN} += "kernel-module-vkms-drv"
