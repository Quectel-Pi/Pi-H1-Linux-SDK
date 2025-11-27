SUMMARY = "Quectel Close prebuild source files"
LICENSE = "CLOSED"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

DEPENDS += "zlib libevdev xxd-native pkgconfig-native"


inherit externalsrc systemd

FILESPATH:prepend =  "${WORKSPACE}/quectel_build:"
SRC_URI = "file://prebuild-files"
S = "${WORKDIR}/prebuild-files"

do_configure () {
    bbnote skip do_configure
}

do_compile() {
    bbnote skip do_configure
}

do_install() {
	bbnote skip install

}

install_files () {
    cp -a  ${WORKSPACE}/quectel_build/prebuild-files/* ${D}/
}

do_install[postfuncs] += "install_files"

PROVIDES += "atci camx camx-kt camxlib-kt chicdk-kt"

RPROVIDES:${PN} += "atci camx camx-kt camxlib-kt chicdk-kt"

SYSTEMD_SERVICE:${PN} = "atci.service"
SYSTEMD_AUTO_ENABLE = "enable"


FILES:${PN} += " \ 
  /* \
"
INSANE_SKIP:${PN} = " all dev-so dev-deps libdir arch"
INSANE_SKIP:${PN} += " arch"
INSANE_SKIP:${PN} += " ldflags usrmerge"
INSANE_SKIP:${PN} += " already-stripped"
INSANE_SKIP:${PN} += " symlink-so  ldflags dev-elf host-user-contaminated"
INSANE_SKIP:${PN} += " file-rdeps"
INSANE_SKIP:${PN}-dbg = "arch"
INSANE_SKIP = "1"
INSANE_SKIP:${PN} = "already-stripped"
#The modules require .so to be dynamicaly loaded
INSANE_SKIP:${PN} += "dev-so"
INSANE_SKIP:${PN} += "file-rdeps"
INSANE_SKIP:${PN}-dbg = "arch"
INSANE_SKIP:${PN} += "arch"
# The modules require .so to be dynamicaly loaded
INSANE_SKIP:${PN} += "dev-so"