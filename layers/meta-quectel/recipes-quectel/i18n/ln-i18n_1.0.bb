SUMMARY = "Shared gettext helper for Quectel's on-board Python GUIs"
DESCRIPTION = "One small module (ln_i18n.py) so the TimeCapsule, NPU Chat, NPU Test \
and Pi Agent GUIs all read the system language the same way. The msgid is the \
Chinese source string, so a Chinese session needs no .mo at all; a non-Chinese \
session loads <domain>.mo from /usr/share/locale. It is a plain file rather than \
a site-packages install because the DEBIAN/UBUNTU rootfs overlays and the LINUX \
Yocto rootfs put python packages in different directories, and the apps find it \
through one explicit path."
HOMEPAGE = "https://www.quectel.com.cn/docs/quectel-pi/h1"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI = "file://ln_i18n.py"

S = "${WORKDIR}"

# Deliberately a single data file with no python dependency: the GUIs import it
# themselves and fall back to a pass-through `_()` if it is missing, so an image
# that loses this package degrades to "all Chinese" instead of failing to start.
do_install() {
    install -d ${D}${datadir}/ln-i18n
    install -m 0644 ${S}/ln_i18n.py ${D}${datadir}/ln-i18n/ln_i18n.py
}

FILES:${PN} = "${datadir}/ln-i18n"
