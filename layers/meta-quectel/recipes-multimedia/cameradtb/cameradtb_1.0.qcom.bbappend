FILESPATH:prepend =  "${WORKSPACE}/sources/quectel-src:"
SRC_URI = "file://camera-devicetree"
S = "${WORKDIR}/camera-devicetree"

do_compile:append() {
    oe_runmake ${EXTRA_OEMAKE} qcm6490-camera-rb3
}