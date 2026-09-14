SUMMARY = "Quectel ATCI server"
LICENSE = "CLOSED"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

DEPENDS += "zlib libevdev xxd-native pkgconfig-native"


inherit externalsrc systemd

LINUX_SRC="${WORKSPACE}/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source"

FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
SRC_URI = "file://atci/ \
		   file://99-partlabel-auto.rules \
          "

S = "${WORKDIR}"

do_configure () {
    bbnote skip do_configure
}

do_compile() {
	cd ${S}/atci/
	oe_runmake clean
	oe_runmake
}

do_install() {
	sleep 10
	install -d ${D}/usr/sbin
	install -m 0755 ${S}/atci/atcid ${D}/usr/sbin
	install -m 0755 ${S}/atci/scripts/atci_init.sh  ${D}/usr/sbin
	install -m 0755 ${S}/atci/scripts/pi-cleanup.sh  ${D}/usr/sbin
	install -m 0755 ${S}/atci/scripts/fan_ctrl.sh  ${D}/usr/sbin
	install -d ${D}${systemd_unitdir}/system/
    install -m 0644 ${S}/atci/scripts/atci.service  ${D}${systemd_unitdir}/system/atci.service
	install -m 0644 ${S}/atci/scripts/auto-time.service ${D}${systemd_unitdir}/system/auto-time.service
	install -m 0644 ${S}/atci/scripts/dconf-update.service ${D}${systemd_unitdir}/system/dconf-update.service
	install -m 0644 ${S}/atci/scripts/pi-cleanup.service ${D}${systemd_unitdir}/system/pi-cleanup.service
	install -m 0644 ${S}/atci/scripts/fan_ctrl.service ${D}${systemd_unitdir}/system/fan_ctrl.service
	install -d ${D}${sysconfdir}/udev/rules.d
    install -m 0644 ${S}/99-partlabel-auto.rules ${D}${sysconfdir}/udev/rules.d/

	install -d ${D}/dev/block/bootdevice/by-name
}

RPROVIDES_${PN} += "atci service"

SYSTEMD_SERVICE:${PN} = "atci.service pi-cleanup.service auto-time.service dconf-update.service fan_ctrl.service"
SYSTEMD_AUTO_ENABLE = "enable"


FILES:${PN} += " \ 
  /usr/sbin/atcid \
  /usr/sbin/atci_init.sh \
  /usr/sbin/pi-cleanup.sh \
  /usr/sbin/fan_ctrl.sh \
  /usr/lib/systemd/system/atci.service \
  /usr/lib/systemd/system/pi-cleanup.service \
  /usr/lib/systemd/system/fan_ctrl.service \
  /usr/lib/systemd/system/auto-time.service \
  /usr/lib/systemd/system/dconf-update.service \
  ${sysconfdir}/udev/rules.d/99-partlabel-auto.rules \
  /dev/block/bootdevice/by-name \
"
INSANE_SKIP:${PN} += "ldflags usrmerge"
deltask do_rm_work
