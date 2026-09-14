SUMMARY = "User space daemon for extended IEEE 802.11 management"
HOMEPAGE = "http://w1.fi/hostapd/"
SECTION = "kernel/userland"
LICENSE = "BSD-3-Clause"
LIC_FILES_CHKSUM = "file://hostapd/README;md5=0e430ef1be3d6eebf257cf493fc7661d"

DEPENDS = "libnl openssl"

FILESEXTRAPATHS:prepend := "${THISDIR}/${BPN}:"

SRC_URI = " \
            http://w1.fi/releases/hostapd-${PV}.tar.gz \
            file://defconfig.qcom \
            file://0001-UPSTREAM-hostapd-Fix-clearing-up-settings-for-color-switch.patch \
"
SRC_URI[sha256sum] = "2b3facb632fd4f65e32f4bf82a76b4b72c501f995a4f62e330219fe7aed1747a"

inherit update-rc.d systemd pkgconfig features_check

CONFLICT_DISTRO_FEATURES = "openssl-no-weak-ciphers"

INITSCRIPT_NAME = "hostapd"

do_configure() {
    install -m 0644 ${WORKDIR}/defconfig.qcom ${B}/hostapd/.config
}

do_compile() {
    export CFLAGS="-MMD -O2 -Wall -g"
    export EXTRA_CFLAGS="${CFLAGS}"
    make -C hostapd V=1
}

do_install() {
    install -d ${D}${sbindir}
    install -m 0755 ${B}/hostapd/hostapd ${D}${sbindir}
    install -m 0755 ${B}/hostapd/hostapd_cli ${D}${sbindir}
}
