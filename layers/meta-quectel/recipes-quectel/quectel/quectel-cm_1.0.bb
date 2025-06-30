DESCRIPTION = "Quectel Connection Manager Tool"
LICENSE = "CLOSED"

inherit cmake systemd

SRC_URI = "file://quectel-CM.tgz file://99-quectel-cm.rules file://quectel-cm@.service"

S = "${WORKDIR}/quectel-CM"

do_install:append () {
    install -d ${D}${sysconfdir}/udev/rules.d
    install -m 0644 ${WORKDIR}/99-quectel-cm.rules ${D}${sysconfdir}/udev/rules.d/99-quectel-cm.rules

    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/quectel-cm@.service ${D}${systemd_system_unitdir}
}

SYSTEMD_SERVICE:${PN} += "quectel-cm@.service"
EXTRA_OECMAKE:append:sdxpinn = " -DUSE_QRTR=1"
