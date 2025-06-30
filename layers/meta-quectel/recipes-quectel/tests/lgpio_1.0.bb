DESCRIPTION = "GPIO Tools"
LICENSE = "CLOSED"

inherit systemd 

DEPENDS += "python3-setuptools-native"

SRC_URI = "git://github.com/joan2937/lg.git;name=lg;protocol=https;branch=master \
           file://rgpiod.service \
          "
SRCREV_lg = "746f0df43774175090b93abcc860b6733eefc09b"
S = "${WORKDIR}/git"

do_configure() {
}

do_compile() {
	sed -i '/^CC\s*=/d' ${S}/Makefile
	sed -i '/^STRIP\s*=/d' ${S}/Makefile
	sed -i '/STRIP/d' ${S}/Makefile
	oe_runmake
}

do_install:append() {
	oe_runmake install DESTDIR=${D} prefix=/usr
	cp -r ${D}/usr/local/* ${D}/usr/
	rm -rf ${D}/usr/local

	install -d ${D}${systemd_system_unitdir}
	install -m 0644 ${WORKDIR}/rgpiod.service ${D}${systemd_system_unitdir}
}

SYSTEMD_SERVICE:${PN} = "rgpiod.service"
SYSTEMD_AUTO_ENABLE:${PN} = "enable"

FILES:${PN} += " \
  /usr/lib/python3.10/dist-packages \
  ${systemd_system_unitdir}/rgpiod.service \
"
