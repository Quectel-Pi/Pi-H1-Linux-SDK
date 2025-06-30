DESCRIPTION = "Quectel tests appliction"
LICENSE = "CLOSED"

inherit cmake pkgconfig

DEPENDS += "libdrm"
RDEPENDS:${PN} += "python3-pyserial"

FILESPATH =+ "${WORKSPACE}/src/files:"
SRC_URI = "file://tests \
          "
S = "${WORKDIR}/tests"

do_compile:prepend() {
	/usr/bin/python ${S}/generate-input.h-labels.py ${PKG_CONFIG_SYSROOT_DIR}/usr/include/linux/input-event-codes.h > ${S}/input.h-labels.h
}

do_install:append() {
	install -d ${D}${bindir}
	install -m 0755  ${S}/ql-test-uart.py ${D}${bindir}/ql-test-uart.py
}
