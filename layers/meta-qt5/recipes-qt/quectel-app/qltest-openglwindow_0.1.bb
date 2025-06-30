SUMMARY = "Simple opengl es demo application"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

DEPENDS += "qtbase"
do_package_qa[noexec] = "1"
do_rm_work[noexec] = "1"

#FILESPATH =+ "${WORKSPACE}:"
SRC_URI = "file://src"

S = "${WORKDIR}/src"

inherit qmake5

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${B}/openglwindow ${D}${bindir}/qltest-openglwindow
}

FILES_${PN} += "${bindir}/*"
