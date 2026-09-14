DEPENDS += " virtual/kernel"
inherit module
DESCRIPTION = "sysdig quectel"
LICENSE = "CLOSED"
SRC_URI = "file://sysdig-0.41.4-aarch64.tar.gz \
           file://Makefile.am"
S = "${WORKDIR}/sysdig-0.41.4-aarch64/usr/src/scap-8.1.0+driver"

EXTRA_OEMAKE += "MACHINE='${MACHINE}'"
EXTRA_OEMAKE += "KERNELDIR=${STAGING_KERNEL_DIR}"

do_compile:prepend() {
    cp ${WORKDIR}/Makefile.am ${S}/Makefile
}

do_install:append() {
	install -d ${D}/usr/bin
	install -m 0755 ${S}/../../bin/csysdig             ${D}/usr/bin/csysdig
	install -m 0755 ${S}/../../bin/scap-driver-loader  ${D}/usr/bin/scap-driver-loader
	install -m 0755 ${S}/../../bin/sysdig              ${D}/usr/bin/sysdig
}

FILES:${PN} += " \
  /usr/bin/csysdig \
  /usr/bin/sysdig \
  /usr/bin/scap-driver-loader \
"

MAKE_TARGETS = "all"
MODULES_INSTALL_TARGET = "install"
RM_WORK_EXCLUDE += "${PN}"
deltask do_rm_work
