SUMMARY = "Self-heal Hexagon DSPs that a previous boot left running"
DESCRIPTION = "A warm reset of the application processor does not power-cycle the \
Hexagon DSPs.  When the CDSP is still running the firmware of the previous boot, \
the first PAS start times out with -110 and /dev/fastrpc-cdsp never appears, which \
takes the NPU down until the board is power cycled.  qcom_q6v5_pas now shuts the \
firmwares down on poweroff/restart; this unit retries the start for the reset \
paths that skip it (panic, watchdog, reboot -f, reset button)."

LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = " \
    file://qcom-remoteproc-recover \
    file://qcom-remoteproc-recover.service \
"

S = "${WORKDIR}"

inherit systemd

SYSTEMD_SERVICE:${PN} = "qcom-remoteproc-recover.service"
# NOTE: set the un-overridden variable, not SYSTEMD_AUTO_ENABLE:${PN}; systemd.bbclass
# interpolates ${SYSTEMD_AUTO_ENABLE} into the generated postinst.
SYSTEMD_AUTO_ENABLE = "enable"

do_install() {
    install -d ${D}${sbindir}
    install -m 0755 ${S}/qcom-remoteproc-recover ${D}${sbindir}/qcom-remoteproc-recover

    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${S}/qcom-remoteproc-recover.service \
        ${D}${systemd_system_unitdir}/qcom-remoteproc-recover.service
}

FILES:${PN} = " \
    ${sbindir}/qcom-remoteproc-recover \
    ${systemd_system_unitdir}/qcom-remoteproc-recover.service \
"
