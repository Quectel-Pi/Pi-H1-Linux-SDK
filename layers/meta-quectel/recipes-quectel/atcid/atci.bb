SUMMARY = "Quectel ATCI server"
LICENSE = "CLOSED"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

DEPENDS += "zlib"

inherit externalsrc systemd qlprebuilt

LINUX_SRC="${WORKSPACE}/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source"


FILESEXTRAPATHS:prepend := "${TOPDIR}/../prebuilt:"

SRC_URI = "file://atci.service"

SRC_URI += "file://prebuilt-${BP}.tar.gz"

S = "${WORKDIR}"

do_configure () {
    bbnote skip do_configure
}

do_compile() {
	cd ${S}/atci/
	oe_runmake clean
	oe_runmake
}



RPROVIDES_${PN} += "atci service"

#SYSTEMD_SERVICE:${PN} = "atci.service"
SYSTEMD_AUTO_ENABLE = "enable"


FILES:${PN} += " \ 
  /usr/sbin/atcid \
  /usr/sbin/atci_init.sh \
  /usr/lib/systemd/system/atci.service \
  /usr/share/licenses \
  /usr/share/licenses/atci \
"
INSANE_SKIP:${PN} += "ldflags usrmerge"
