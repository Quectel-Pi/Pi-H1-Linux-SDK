FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI:append:qcom = " \
    file://usb_automount.sh \
    file://usb-automount.service \
"

do_install:append:qcom() {
    install -d ${D}${sbindir}
    install -m 0755 ${WORKDIR}/usb_automount.sh ${D}${sbindir}/usb_automount.sh
    install -d ${D}${systemd_unitdir}/system
    install -m 0644 ${WORKDIR}/usb-automount.service ${D}${systemd_unitdir}/system/usb-automount.service
    install -d ${D}${systemd_unitdir}/system/multi-user.target.wants
    ln -sf ${systemd_unitdir}/system/usb-automount.service ${D}${systemd_unitdir}/system/multi-user.target.wants/usb-automount.service
}

PACKAGES =+ "${PN}-automount-ucard"
FILES:${PN}-automount-ucard =+ "${sbindir}/usb_automount.sh ${systemd_unitdir}/system/usb-automount.service ${systemd_unitdir}/system/multi-user.target.wants/usb-automount.service"
