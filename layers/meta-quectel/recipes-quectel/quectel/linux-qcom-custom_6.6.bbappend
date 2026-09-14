LICENSE = "CLOSED"

FILESPATH =+ "${WORKSPACE}/sources/quectel-src/kernel:"
SRC_URI = "file://qcom-6.6"

S = "${WORKDIR}/qcom-6.6"

FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
SRC_URI += "file://kernel-cfg-for-qmi_wwan_q.cfg \
            file://kernel-cfg-for-wlan.cfg \
            file://kernel-cfg-for-debian.cfg \
            file://kernel-cfg-joydev.cfg \
            file://kernel-cfg-display.cfg \
            "

KERNEL_CONFIG_FRAGMENTS:append = " ${WORKDIR}/kernel-cfg-for-qmi_wwan_q.cfg"
KERNEL_CONFIG_FRAGMENTS:append = " ${WORKDIR}/kernel-cfg-for-wlan.cfg"
KERNEL_CONFIG_FRAGMENTS:append = " ${WORKDIR}/kernel-cfg-for-debian.cfg"
KERNEL_CONFIG_FRAGMENTS:append = " ${WORKDIR}/kernel-cfg-joydev.cfg"
KERNEL_CONFIG_FRAGMENTS:append = " ${WORKDIR}/kernel-cfg-display.cfg"


# STD/WESTON模式(SKIP=1)保留base recipe的msm黑名单配置，其他情况移除
python () {
    skip = d.getVar('SKIP_DEPLOY_DEBIAN_GNOME_ROOTFS')
    if skip != '1':
        d.setVarFlag('KERNEL_MODULE_PROBECONF', 'remove', 'msm')
        d.setVar('module_conf_msm', '')
}