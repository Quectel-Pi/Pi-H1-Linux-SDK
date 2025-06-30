FILESPATH:prepend =  "${WORKSPACE}/src:"
SRC_URI = "file://display-drivers"
S = "${WORKDIR}/display-drivers"

do_install:append() {
	# From meta-qcom-hwe/recipes-kernel/linux-firmware/linux-firmware_20241017.bb
	install -d ${D}/${nonarch_base_libdir}/firmware
	install -m 0755 ${S}/bridge-drivers/lt9611uxc_fw.bin ${D}/${nonarch_base_libdir}/firmware/lt9611uxc_fw.bin
	# From https://docs.qualcomm.com/bundle/publicresource/topics/80-70017-18SC/debug.html
	install -m 0755 ${S}/bridge-drivers/LT9611UXC_DSI_PortA_HDCP_Disable_V5.0.21.bin ${D}/${nonarch_base_libdir}/firmware/lt9611uxc_fw.bin
}

FILES:${PN} += "${nonarch_base_libdir}/firmware/lt9611uxc_fw.bin"
