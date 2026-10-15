FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
SRC_URI += "file://0001-hciattach-add-support-for-qualcomm-chip.patch \
            file://bt-autoconnect.sh \
            file://bt-autoconnect.service \
"

do_install:append() {
    install -d ${D}${bindir}
    install -m 755 ${B}/tools/hciattach ${D}/${bindir}/qcom-hciattach

    # Paired Bluetooth audio sinks never come back after a reboot: BlueZ's own
    # reconnect (main.conf [Policy] ReconnectUUIDs) only fires on link loss, so
    # the speaker has to be power-cycled to be seen again. Push it up instead.
    install -m 0755 ${WORKDIR}/bt-autoconnect.sh ${D}${bindir}/bt-autoconnect
    install -D -m 0644 ${WORKDIR}/bt-autoconnect.service \
        ${D}${systemd_system_unitdir}/bt-autoconnect.service

    # Ship the wants link ourselves, same as meta-qcom-hwe does for
    # qca_set_bdaddr.service, so enablement does not depend on the offline
    # systemctl run in systemd.bbclass' postinst.
    install -d ${D}${sysconfdir}/systemd/system/multi-user.target.wants
    ln -sf ${systemd_system_unitdir}/bt-autoconnect.service \
        ${D}${sysconfdir}/systemd/system/multi-user.target.wants/bt-autoconnect.service
}

SYSTEMD_SERVICE:${PN} += "bt-autoconnect.service"
