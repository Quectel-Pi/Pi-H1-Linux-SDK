SUMMARY = "Launcher and first-run installer for the Pi coding agent"
DESCRIPTION = "Gives the board a 'Pi 编程助手' icon in the application grid. \
Nothing is preinstalled: clicking the icon while pi is missing opens a GTK4 \
wizard that offers to install it, and once pi is installed and configured the \
same icon opens a terminal running pi. The wizard downloads the Node.js \
runtime and pi itself into the user's home directory when the person in front \
of the board agrees, offers 'ask me later' and 'don't install', and after a \
successful run asks for the API URL, key and model, writes \
~/.pi/agent/{models,auth,settings}.json and shows how to start pi. Removing it \
is deleting ~/.local/share/pi and ~/.local/bin/pi: nothing outside $HOME is \
touched, so no root, no polkit prompt, no apt."
HOMEPAGE = "https://pi.dev"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI = " \
    file://pi-agent \
    file://pi-agent-setup.py \
    file://engine.py \
    file://pi-agent.desktop \
    file://pi-agent-setup-autostart.desktop \
    file://pi-agent.svg \
    file://README.md \
"

S = "${WORKDIR}"

# Deliberately not 'inherit allarch': the package is shell + python, so allarch
# would be legitimate, but this image mixes Yocto-built debs with a prebuilt
# distro rootfs and the deb-track lists key off the deployed arch directory.
# Staying at the default MACHINE_ARCH keeps this package next to pi-voice-input
# and friends in deploy/deb/armv8-2a + qcm6490*, which is what
# bsp-userspace.txt is validated against.

# Runtime deps follow the OS dimension, exactly like npu-llm's gtk4 handling:
#
#   LINUX   pure OE rootfs -> take OE's python3 + gtk4 + pygobject
#   DEBIAN  the sample rootfs already ships python3 3.13, python3-gi 3.50,
#   UBUNTU  gir1.2-gtk-4.0 and gir1.2-adw-1 -> RDEPEND on nothing
#
# RDEPENDing on OE's gtk4 on an overlay build would drag a second GTK stack
# into an image that already has one, and the rsync that lays the distro
# tarball down runs without --delete, so the OE copies would survive as
# unowned files next to the distro's (this is the failure npu-llm documents).
# The wizard degrades with a clear error if a module is genuinely missing, so
# the overlay case is safe: python3-gi is part of the GNOME desktop there.
#
# python3-compression carries tarfile (the node tarball is .tar.xz, whose lzma
# module lives in python3-core); python3-netclient carries urllib.request,
# which is how downloads are made. The Debian 13 sample rootfs has no
# /usr/bin/curl at all, so shelling out to curl was never an option.
#
# x-terminal-emulator is what the launcher actually starts on the overlay
# images (an alternatives symlink to gnome-terminal there); it is listed only
# for the LINUX rootfs because Debian's package of that name comes from the
# distro itself. The launcher falls back through gnome-terminal/kgx/xterm
# rather than hard-failing if none of them exists.
RDEPENDS:${PN} = " \
    ${@ 'python3-core python3-io python3-compression python3-netclient \
           python3-json python3-pygobject gtk4 adwaita-icon-theme' \
         if d.getVar('QUECTEL_OS_DIM') == 'LINUX' else ''} \
"

# libadwaita is used when present (toast overlay) and skipped otherwise, so a
# hard dependency would be wrong; the wizard checks at runtime. Only the pure
# OE rootfs has the recipe to recommend.
RRECOMMENDS:${PN} = "${@ 'libadwaita' if d.getVar('QUECTEL_OS_DIM') == 'LINUX' else ''}"

# The icon goes into the theme the desktop actually searches. The class only
# refreshes the cache and does not pull anything in beyond what GTK needs.
do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${S}/pi-agent ${D}${bindir}/pi-agent
    install -m 0755 ${S}/pi-agent-setup.py ${D}${bindir}/pi-agent-setup

    install -d ${D}${datadir}/pi-agent-setup
    install -m 0644 ${S}/engine.py ${D}${datadir}/pi-agent-setup/engine.py

    # The application-grid entry and the icon it shows.
    install -d ${D}${datadir}/applications ${D}${datadir}/icons/hicolor/scalable/apps
    install -m 0644 ${S}/pi-agent.desktop ${D}${datadir}/applications/pi-agent.desktop
    install -m 0644 ${S}/pi-agent.svg \
        ${D}${datadir}/icons/hicolor/scalable/apps/pi-agent.svg

    # First-login prompt. It starts pi-agent, not the wizard, so the "installed
    # but never configured" case lands on the endpoint page rather than being
    # asked to install again.
    install -d ${D}${sysconfdir}/xdg/autostart
    install -m 0644 ${S}/pi-agent-setup-autostart.desktop \
        ${D}${sysconfdir}/xdg/autostart/pi-agent-setup.desktop

    install -d ${D}${docdir}/${PN}
    install -m 0644 ${S}/README.md ${D}${docdir}/${PN}/README.md
}

FILES:${PN} = " \
    ${bindir}/pi-agent \
    ${bindir}/pi-agent-setup \
    ${datadir}/pi-agent-setup \
    ${datadir}/applications/pi-agent.desktop \
    ${datadir}/icons/hicolor/scalable/apps/pi-agent.svg \
    ${sysconfdir}/xdg/autostart/pi-agent-setup.desktop \
"

# README goes into the -doc subpackage that ${docdir} implies. Keeping it out
# of the main package is deliberate: this is a launcher, and the main package
# should stay tiny so it can be switched off by removing one package. The
# install instructions are also in the wizard itself (last page), which is what
# people actually read.
FILES:${PN}-doc = "${docdir}/${PN}"

# No postinst is needed: the icon is a plain file in hicolor, the .desktop files
# are read from disk by the shell, and the launcher creates its workspace on
# first use. Nothing here needs to run as root at install time.
INSANE_SKIP:${PN} += "already-stripped"
