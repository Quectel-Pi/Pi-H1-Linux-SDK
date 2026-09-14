SUMMARY = "V4L2 Loopback - creates virtual video devices"
DESCRIPTION = "Kernel module that creates virtual video devices for bridging camera to V4L2 applications"
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://COPYING;md5=b234ee4d69f5fce4486a80fdaf4a4263"

FILESPATH:prepend = "${TOPDIR}/../sources/open-src:"
SRC_URI = "git://github.com/super617/v4l2loopback.git;branch=main;protocol=https"
SRCREV = "516e6f1c207ebe31a82cfa7552829ef25bdd1a55"
S = "${WORKDIR}/git"

inherit module-base

DEPENDS += "virtual/kernel"
KERNEL_MODULE_AUTOLOAD += "v4l2loopback"

do_compile() {
    oe_runmake -C ${STAGING_KERNEL_BUILDDIR} M=${S} modules
}

do_install() {
    install -d ${D}${nonarch_base_libdir}/modules/${KERNEL_VERSION}/extra
    install -m 0644 ${S}/v4l2loopback.ko ${D}${nonarch_base_libdir}/modules/${KERNEL_VERSION}/extra/

    install -d ${D}${sysconfdir}/modprobe.d
    cat > ${D}${sysconfdir}/modprobe.d/v4l2loopback.conf << 'EOF'
# v4l2loopback default configuration
options v4l2loopback video_nr=10 exclusive_caps=1 card_label="CamxBridge"
EOF
}

FILES:${PN} += "${nonarch_base_libdir}/modules/${KERNEL_VERSION}/extra/v4l2loopback.ko"
FILES:${PN} += "${sysconfdir}/modprobe.d"

RDEPENDS:${PN} = ""
