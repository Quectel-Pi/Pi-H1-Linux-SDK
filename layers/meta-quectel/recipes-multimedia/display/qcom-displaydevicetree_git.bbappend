FILESPATH:prepend =  "${WORKSPACE}/sources/quectel-src:"
SRC_URI = "file://display-devicetree"
S = "${WORKDIR}/display-devicetree"

do_compile:append() {
	oe_runmake ${EXTRA_OEMAKE} qcm6490-display-pi
}
