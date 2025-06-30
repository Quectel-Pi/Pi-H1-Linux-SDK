SUMMARY = "Quectel FC2X Wi-Fi Driver"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

DEPENDS += "zlib libnl openssl bluez5 virtual/kernel"

inherit module externalsrc systemd

LINUX_SRC="${WORKSPACE}/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source"

FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
SRC_URI = "file://WiFi/ \
           file://0001_fc2x-driver-kernel-6.6.patch \
		   file://0001-add-support-shadow-dev-addr-feature.patch \
          "

S = "${WORKDIR}"
TOOLS_DIR = "${S}/WiFi/cnss_host_LEA/chss_proc/host/tools"
FIRMWARE_DIR = "${S}/WiFi/meta_build/load_meta"

EXTRA_OEMAKE += "${PARALLEL_MAKE}"
EXTRA_OEMAKE += "TOOL_EXTRA_CFLAGS=' -I${WORKDIR}/recipe-sysroot/usr/include -I${WORKDIR}/recipe-sysroot/usr/include/libnl3'"

do_configure () {
    bbnote skip do_configure
}

do_compile() {
	echo quectel fc6xe start compile
	cd ${S}/WiFi/cnss_host_LEA/chss_proc/host/AIO/build
	oe_runmake CONFIG_PERF_BUILD=y drivers KERNELARCH=${ARCH} KERNELPATH=${LINUX_SRC} TOOLPREFIX=${CROSS_COMPILE}
	cd ${S}/WiFi/cnss_host_LEA/chss_proc/host/tools/myftm/ath6kl-utils
	oe_runmake clean
	oe_runmake
	cd ${S}/WiFi/cnss_host_LEA/chss_proc/host/tools/btdiag
	oe_runmake clean
	oe_runmake
	cd ${S}/WiFi/cnss_host_LEA/chss_proc/host/tools/Qcmbr
	oe_runmake clean
	oe_runmake
	cd ${S}/WiFi/cnss_host_LEA/chss_proc/host/tools/btconfig
	oe_runmake clean
	oe_runmake
	cd ${S}/WiFi/cnss_host_LEA/chss_proc/host/AIO/apps/hostap/hostapd
	oe_runmake clean
	cp defconfig .config
	oe_runmake
	cd ${S}/WiFi/cnss_host_LEA/chss_proc/host/AIO/apps/hostap/wpa_supplicant
	oe_runmake clean
	cp defconfig .config
	oe_runmake
	echo quectel fc6xe end compile
}

do_install() {
	install -d ${D}/lib/firmware/wlan
	install -d ${D}/lib/firmware
	install ${FIRMWARE_DIR}/wlan_firmware/sdio/*.bin ${D}/lib/firmware
	install ${FIRMWARE_DIR}/bdf/FC900EABMD/bdwlan30.bin ${D}/lib/firmware
	install ${FIRMWARE_DIR}/host/wlan_host/sdio/qcom_cfg.ini ${D}/lib/firmware/wlan
	install -d ${D}${systemd_unitdir}/system/
    install -m 0644 ${S}/WiFi/scripts/wlan-mac-check.service ${D}${systemd_unitdir}/system/wlan-mac-check.service
	install -d ${D}/usr/sbin
	install -m 0755 ${TOOLS_DIR}/Qcmbr/Qcmbr ${D}/usr/sbin
	install -m 0755 ${TOOLS_DIR}/btconfig/btconfig ${D}/usr/sbin
	install -m 0755 ${TOOLS_DIR}/btdiag/Btdiag ${D}/usr/sbin
	install -m 0755 ${TOOLS_DIR}/myftm/ath6kl-utils/myftm/myftm ${D}/usr/sbin
	install -m 0755 ${S}/WiFi/scripts/check_mac_addr ${D}/usr/sbin
	install -m 0755 ${S}/WiFi/cnss_host_LEA/chss_proc/host/AIO/apps/hostap/hostapd/hostapd ${D}/usr/sbin
	install -m 0755 ${S}/WiFi/cnss_host_LEA/chss_proc/host/AIO/apps/hostap/wpa_supplicant/wpa_cli ${D}/usr/sbin
	install -m 0755 ${S}/WiFi/cnss_host_LEA/chss_proc/host/AIO/apps/hostap/wpa_supplicant/wpa_supplicant ${D}/usr/sbin
	install -d ${D}/lib/modules/6.6.52-qli-1.3-ver.1.1/updates
	install -m 0644 ${S}/WiFi/cnss_host_LEA/chss_proc/host/AIO/drivers/qcacld-new/wlan.ko  ${D}/lib/modules/6.6.52-qli-1.3-ver.1.1/updates/wlan.ko
	install -d ${D}/lib/firmware/qca
	install -m 0755 ${S}/WiFi/meta_build/load_meta/bt_firmware/* ${D}/lib/firmware/qca
}

RPROVIDES_${PN} += "kernel-module-qca1023"

SYSTEMD_SERVICE:${PN} = "wlan-mac-check.service"
SYSTEMD_AUTO_ENABLE = "enable"

INITSCRIPT_NAME = "wlan-mac-check"
INITSCRIPT_PARAMS = "defaults"

FILES:${PN} += " \ 
  /lib/firmware/* \
  /lib/firmware/wlan/qcom_cfg.ini \
  /usr/sbin/Qcmbr \
  /usr/sbin/btconfig \
  /usr/sbin/Btdiag \
  /usr/sbin/myftm \
  /usr/sbin/wpa_cli \
  /usr/sbin/hostapd \
  /usr/sbin/wpa_supplicant \
  /usr/sbin/check_mac_addr \
  /lib/modules/6.6.52-qli-1.3-ver.1.1/updates/wlan.ko \
  /usr/lib/systemd/system/wlan-mac-check.service \
"
INSANE_SKIP:${PN} += "ldflags usrmerge"
INSANE_SKIP:qca1023-wlan-dbg += "ldflags usrmerge"
