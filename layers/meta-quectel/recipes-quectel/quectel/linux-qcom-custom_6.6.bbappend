LICENSE = "CLOSED"

FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
SRC_URI += "file://kernel-cfg-for-qmi_wwan_q.cfg \
            file://kernel-cfg-for-wlan.cfg "

KERNEL_CONFIG_FRAGMENTS:append = " ${WORKDIR}/kernel-cfg-for-qmi_wwan_q.cfg"
KERNEL_CONFIG_FRAGMENTS:append = " ${WORKDIR}/kernel-cfg-for-wlan.cfg"