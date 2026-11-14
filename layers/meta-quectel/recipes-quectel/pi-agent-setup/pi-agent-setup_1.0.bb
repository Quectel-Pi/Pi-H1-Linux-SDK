SUMMARY = "Launcher, first-run installer and board skills for the Pi Agent"
DESCRIPTION = "Gives the board a 'Pi Agent' icon in the application grid. \
Nothing is preinstalled: clicking the icon while pi is missing opens a GTK4 \
wizard that offers to install it, and once pi is installed and configured the \
same icon opens a terminal running pi. The wizard downloads the Node.js \
runtime and pi itself into the user's home directory when the person in front \
of the board agrees, offers 'ask me later' and 'don't install', and after a \
successful run asks for the API URL, key and model, writes \
~/.pi/agent/{models,auth,settings}.json and shows how to start pi. Removing it \
is deleting ~/.local/share/pi and ~/.local/bin/pi: nothing outside $HOME is \
touched, so no root, no polkit prompt, no apt. \
\
The same package also teaches the board about its own peripherals: the skill \
set (sysfs, systemctl, wpctl, v4l2, I2C/GPIO, QNN) that pi reads on the device \
rather than the SDK-host skills in quectel_build/skills/. That set is NOT \
shipped in the image -- the repository it lives in carries no licence, so it \
cannot be redistributed inside a firmware image. The installer asks once \
whether to fetch it, the login hook asks again before pulling a newer \
revision, and nothing is downloaded unless the person says yes."
HOMEPAGE = "https://pi.dev"
LICENSE = "MIT"
# Pi Agent itself (@earendil-works/pi-coding-agent) is MIT but its npm tarball
# ships no LICENSE file, and this wizard is MIT too -- so the recipe carries its
# own copy instead of pointing at ${COMMON_LICENSE_DIR}/MIT. The file has a short
# comment header saying exactly that, which is why its md5 differs from the
# generic MIT text.
LIC_FILES_CHKSUM = "file://LICENSE.pi-agent;md5=3983bed0ec79107eb220e13ada79e95a"

FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI = " \
    file://pi-agent \
    file://pi-agent-setup.py \
    file://engine.py \
    file://pi-agent.desktop \
    file://pi-agent-setup-autostart.desktop \
    file://pi-agent.svg \
    file://README.md \
    file://LICENSE.pi-agent \
    file://locale/en/LC_MESSAGES/pi-agent-setup.mo \
"

# The board skill set is deliberately NOT fetched at build time and NOT
# installed into the image.
#
# It lives in a third-party repository (github.com/super617/pi-skills) that
# carries no licence file of any kind -- no LICENSE, no COPYING, nothing in the
# README. Copyright therefore defaults to "all rights reserved", and copying
# that content into a firmware image would mean redistributing it without
# permission. Shipping the image would become a licensing problem for every
# unit built from it.
#
# So the image now carries only the *ability* to fetch the skills:
#
#   * the installer asks once whether to fetch them (opt-in, in the wizard),
#   * the login hook asks again before pulling a newer revision,
#   * engine.py does the fetch into $HOME/.pi/agent/pi-skills at runtime,
#   * nothing is downloaded unless the person answers yes.
#
# Bumping a pinned revision here is no longer how you update the skills: they
# are not in here. To publish a new skill set, push to that repository.

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
    ln-i18n \
    bash \
"

# libadwaita is used when present (toast overlay) and skipped otherwise, so a
# hard dependency would be wrong; the wizard checks at runtime. Only the pure
# OE rootfs has the recipe to recommend.
#
# git is a Recommends and not a Depends: engine.py can fetch the skill set as a
# tarball over urllib alone, and git only makes later updates incremental. The
# Ubuntu 26 rootfs has no git at all, so hard-depending would drag it into the
# rsync overlay for a capability the tarball path already provides.
RRECOMMENDS:${PN} = " \
    ${@ 'libadwaita' if d.getVar('QUECTEL_OS_DIM') == 'LINUX' else ''} \
    git \
"

# The icon goes into the theme the desktop actually searches. The class only
# refreshes the cache and does not pull anything in beyond what GTK needs.
do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${S}/pi-agent ${D}${bindir}/pi-agent
    install -m 0755 ${S}/pi-agent-setup.py ${D}${bindir}/pi-agent-setup

    install -d ${D}${datadir}/pi-agent-setup
    install -m 0644 ${S}/engine.py ${D}${datadir}/pi-agent-setup/engine.py

    # English translations. The .mo is a checked-in deliverable, not a build
    # artifact: the DEBIAN/UBUNTU images are rsync overlays that never run this
    # recipe's do_compile, and the apps read it from /usr/share/locale directly.
    # Chinese needs no .mo -- the msgid is the Chinese text.
    install -d ${D}${datadir}/locale/en/LC_MESSAGES
    install -m 0644 ${S}/locale/en/LC_MESSAGES/pi-agent-setup.mo \
        ${D}${datadir}/locale/en/LC_MESSAGES/pi-agent-setup.mo

    # License, in two places on purpose: ${docdir} is where license bookkeeping
    # looks, and ${datadir}/licenses/pi-agent/LICENSE is the path the wizard's
    # "View license" button opens (LICENSE_PATH in pi-agent-setup.py). Keep the
    # two in sync with that constant if it ever moves.
    install -d ${D}${docdir}/${PN} ${D}${datadir}/licenses/pi-agent
    install -m 0644 ${S}/LICENSE.pi-agent ${D}${docdir}/${PN}/copyright
    install -m 0644 ${S}/LICENSE.pi-agent \
        ${D}${datadir}/licenses/pi-agent/LICENSE

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

    # No skill content is installed. See the note above SRC_URI: the skill
    # repository has no licence, so nothing from it may be redistributed inside
    # this image. engine.py fetches it into the user's home at runtime, and only
    # after they agree to it.
}

FILES:${PN} = " \
    ${bindir}/pi-agent \
    ${bindir}/pi-agent-setup \
    ${datadir}/pi-agent-setup \
    ${datadir}/applications/pi-agent.desktop \
    ${datadir}/icons/hicolor/scalable/apps/pi-agent.svg \
    ${datadir}/locale/en/LC_MESSAGES/pi-agent-setup.mo \
    ${datadir}/licenses/pi-agent \
    ${sysconfdir}/xdg/autostart/pi-agent-setup.desktop \
"

# README goes into the -doc subpackage that ${docdir} implies. Keeping it out
# of the main package is deliberate: this is a launcher, and the main package
# should stay tiny so it can be switched off by removing one package. The
# install instructions are also in the wizard itself (last page), which is what
# people actually read.
FILES:${PN}-doc = "${docdir}/${PN}"

# The package is shell plus Python; nothing in it is a compiled binary, so the
# usual strip check has nothing to say.
INSANE_SKIP:${PN} += "already-stripped"

# No postinst, for either half:
#   - the icon is a plain file in hicolor and the .desktop files are read from
#     disk, so the launcher needs nothing done at install time;
#   - linking the skills into ~/.pi/agent/skills/ is per-user state and only
#     applies once the person has agreed to fetch them. Doing it as root in
#     postinst would leave root-owned links inside somebody's home directory,
#     and would fetch third-party content nobody asked for. The pi-agent
#     launcher does the linking for the user who logs in.
