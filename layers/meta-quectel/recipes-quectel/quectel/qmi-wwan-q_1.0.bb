SUMMARY = "usb net qmi_wwan_q for Quectel modules"
LICENSE = "CLOSED"

inherit module

SRC_URI = "file://qmi_wwan_q.tgz"
S = "${WORKDIR}/qmi_wwan_q"

EXTRA_OEMAKE += "KDIR=${STAGING_KERNEL_DIR}"

RPROVIDES_${PN} += "kernel-module-qmi_wwan_q"
KERNEL_MODULE_AUTOLOAD += "qmi_wwan_q"
