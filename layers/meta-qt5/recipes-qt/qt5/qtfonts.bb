#
# This file was derived from the 'Hello World!' example recipe in the
# Yocto Project Development Manual.
#

SUMMARY = "Add fonts/*ttf to /usr/lib/qt5/fonts"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

FILESPATH =+ "${WORKSPACE}:"
SRC_URI = "file://fonts"

S = "${WORKDIR}"
SRC_DIR = "${WORKSPACE}/fonts"

FILES:${PN} += "/usr/lib/*"

do_install:append() {
    install -d ${D}${libdir}/fonts
    install -m 0755 ${WORKDIR}/fonts/* -D ${D}${libdir}/fonts
}
