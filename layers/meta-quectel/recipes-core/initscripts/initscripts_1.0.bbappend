FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI:append:qcom = " \
    file://automountucard.rules \
    file://automountucard.sh \
    file://ucard-mount@.service \
"

do_install:append:qcom() {
    install -d ${D}${libdir}/udev/rules.d
    install -m 0644 ${WORKDIR}/automountucard.rules ${D}${libdir}/udev/rules.d/automountucard.rules
    install -d ${D}${bindir}
    install -m 0755 ${WORKDIR}/automountucard.sh ${D}${bindir}/automountucard.sh
    install -d ${D}${systemd_unitdir}/system
    install -m 0755 ${WORKDIR}/ucard-mount@.service ${D}${systemd_unitdir}/system/ucard-mount@.service
}

PACKAGES =+ "${PN}-automount-ucard"
FILES:${PN}-automount-ucard =+ "${libdir}/udev/rules.d/automountucard.rules ${bindir}/automountucard.sh ${systemd_unitdir}/system/ucard-mount@.service"
