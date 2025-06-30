SUMMARY = "Quectel Key handle server"
LICENSE = "CLOSED"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

DEPENDS += "zlib libevdev"

inherit externalsrc systemd

LINUX_SRC="${WORKSPACE}/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source"

FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
SRC_URI = "file://key-event-handler/ \
          "

S = "${WORKDIR}"

do_configure () {
    bbnote skip do_configure
}

do_compile() {
	cd ${S}/key-event-handler/
	oe_runmake clean
	oe_runmake
}

do_install() {
	install -d ${D}/usr/sbin
	install -m 0755 ${S}/key-event-handler/key_event_handler ${D}/usr/sbin
	install -d ${D}${systemd_unitdir}/system/
    install -m 0644 ${S}/key-event-handler/scripts/key_event_handler.service  ${D}${systemd_unitdir}/system/key_event_handler.service
}

RPROVIDES_${PN} += "key_event_handler service"

SYSTEMD_SERVICE:${PN} = "key_event_handler.service"
SYSTEMD_AUTO_ENABLE = "enable"


FILES:${PN} += " \ 
  /usr/sbin/key_event_handler \
  /usr/lib/systemd/system/key_event_handler.service \
"
INSANE_SKIP:${PN} += "ldflags usrmerge"
