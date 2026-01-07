LICENSE = "CLOSED"

FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
SRC_URI += "file://kernel-cfg-for-qmi_wwan_q.cfg \
            file://kernel-cfg-for-wlan.cfg \
            file://kernel-cfg-for-debian.cfg \
            file://kernel-cfg-joydev.cfg \
            file://CVE-2025-21692.patch \
            file://CVE-2024-53150.patch \
            file://CVE-2024-50302.patch \
            file://CVE-2024-25742.patch \
            file://CVE-2025-37947.patch \
            file://CVE-2023-37454.patch \
            "

KERNEL_CONFIG_FRAGMENTS:append = " ${WORKDIR}/kernel-cfg-for-qmi_wwan_q.cfg"
KERNEL_CONFIG_FRAGMENTS:append = " ${WORKDIR}/kernel-cfg-for-wlan.cfg"
KERNEL_CONFIG_FRAGMENTS:append = " ${WORKDIR}/kernel-cfg-for-debian.cfg"
KERNEL_CONFIG_FRAGMENTS:append = " ${WORKDIR}/kernel-cfg-joydev.cfg"