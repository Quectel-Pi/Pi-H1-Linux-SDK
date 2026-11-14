SUMMARY = "Quectel OTA services with TimeCapsule GUI"
DESCRIPTION = "Btrfs subvolume snapshot A/B upgrade, rollback and restore. \
Includes TimeCapsule, a GTK3 GUI for OTA management (version list, manual \
rollback, file-based upgrade, backup restore)."
LICENSE = "CLOSED"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"


inherit systemd


FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
SRC_URI = "file://ota_run.sh \
	   file://quectel-ota.service \
	   file://current_vol \
	   file://system_upgrade.sh \
	   file://ota_gui.py \
	   file://ota-gui.desktop \
	   file://ota-gui.svg \
	   file://ota_kernel_install.sh \
	   file://locale/en/LC_MESSAGES/quectel-ota-gui.mo \
          "

S = "${WORKDIR}"

# Keep the English .mo inside the main package. OE's package_do_split_locales()
# would otherwise move ${datadir}/locale/en into <PN>-locale-en, which the main
# package only RRECOMMENDS (never RDEPENDs) -- so the .mo would be built, shipped
# as a separate deb, and simply never installed on the image. This recipe has no
# other translations, so nothing else loses its split.
#
# PACKAGE_NO_LOCALE only stops the per-language split; ${PN}-locale stays in
# PACKAGES with the default FILES of ${datadir}/locale and is claimed *before*
# ${PN} (populate_packages sorts -src/-dbg first, then declaration order, and
# ${PN}-locale is declared ahead of ${PN}). Emptying its FILES is what actually
# leaves the .mo to the main package -- dropping just one of these two lines
# silently ships a translation file that nothing installs.
PACKAGE_NO_LOCALE = "1"
FILES:${PN}-locale = ""

do_install() {
	sleep 10
	install -d ${D}/usr/sbin
	install -m 0755 ${S}/ota_run.sh ${D}/usr/sbin/ota_run

	install -d ${D}${systemd_unitdir}/system/
    install -m 0644 ${S}/quectel-ota.service  ${D}${systemd_unitdir}/system/quectel-ota.service

	install -d ${D}/opt
	install -m 0644 ${S}/current_vol ${D}/opt/current_vol
	install -m 0755 ${S}/system_upgrade.sh  ${D}/opt/system_upgrade.sh

	# ===== TimeCapsule GUI =====
	install -d ${D}${bindir}
	install -m 0755 ${S}/ota_gui.py ${D}${bindir}/quectel-ota-gui

	# .desktop entry (appears in GNOME application menu)
	install -d ${D}${datadir}/applications
	install -m 0644 ${S}/ota-gui.desktop ${D}${datadir}/applications/

	# Vector icon (scalable)
	install -d ${D}${datadir}/icons/hicolor/scalable/apps
	install -m 0644 ${S}/ota-gui.svg ${D}${datadir}/icons/hicolor/scalable/apps/

	# English UI translation for the GUI. msgid = Chinese source text, so zh_CN
	# sessions need no .mo at all; every other language reads this one via
	# ln_i18n (see recipes-quectel/i18n).
	install -d ${D}${datadir}/locale/en/LC_MESSAGES
	install -m 0644 ${S}/locale/en/LC_MESSAGES/quectel-ota-gui.mo \
		${D}${datadir}/locale/en/LC_MESSAGES/quectel-ota-gui.mo

	# 内核/脚本安装助手 (root 调用, GUI 经 sudo 提权执行)
	install -m 0755 ${S}/ota_kernel_install.sh ${D}/usr/sbin/ota_kernel_install
}

RPROVIDES_${PN} += "ota service"

# Runtime deps for the GUI.
# The Debian GNOME rootfs overlay already supplies GTK3, pygobject and
# pycairo at runtime, so they are RRECOMMENDS (soft) to tolerate the
# dpkg-provided names and avoid expanding the OE dependency graph.
RRECOMMENDS:${PN} = " \
    gtk+3 \
    python3-pygobject \
    python3-pycairo \
"
RDEPENDS:${PN} = " \
    python3 \
    bash \
    ln-i18n \
"

SYSTEMD_SERVICE:${PN} = "quectel-ota.service"
SYSTEMD_AUTO_ENABLE = "enable"


FILES:${PN} += "/"

INSANE_SKIP:${PN} += "ldflags usrmerge"
