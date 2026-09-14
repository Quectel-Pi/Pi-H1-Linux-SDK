DESCRIPTION = "Quectel HDMI firmware install"
LICENSE = "CLOSED"

inherit systemd

SRC_URI = "file://LT9611UXC_DSI_PortA_HDCP_Disable_V5.0.21.bin \
            file://LT9611UXD.bin"


do_install:append () {
	install -d ${D}/${nonarch_base_libdir}/firmware
	install -m 0755  ${WORKDIR}/LT9611UXC_DSI_PortA_HDCP_Disable_V5.0.21.bin ${D}/${nonarch_base_libdir}/firmware/lt9611uxc_fw.bin
	install -m 0755  ${WORKDIR}/LT9611UXD.bin ${D}/${nonarch_base_libdir}/firmware/LT9611UXD.bin
}

PACKAGES = "${PN}"
FILES:${PN} = "/"