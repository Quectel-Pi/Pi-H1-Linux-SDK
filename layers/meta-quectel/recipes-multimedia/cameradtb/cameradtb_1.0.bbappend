FILESPATH:prepend =  "${WORKSPACE}/src/devicetree:"
SRC_URI = "file://camera-devicetree"
S = "${WORKDIR}/camera-devicetree"

do_compile:append() {
    if [ "${SOC_FAM}" = "qcm6490" ]; then
        if [ "${TARGET_BOARD}" = "qcm6490-idp" ]; then
            oe_runmake ${EXTRA_OEMAKE} qcm6490-camera-rb3
    fi
        fi
}
