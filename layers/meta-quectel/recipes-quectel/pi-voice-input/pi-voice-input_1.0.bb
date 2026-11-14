SUMMARY = "Push-to-talk voice input: hold Ctrl+Q to dictate, release to insert"
DESCRIPTION = "Background dictation service for the Quectel PI H1 (QCS6490) \
Debian/Ubuntu GNOME images. A GNOME status-area switch turns it on and off; \
turning it off frees the speech model, turning it on loads it. While it is on, \
holding Ctrl+Q records from the microphone and releasing inserts the text into \
whatever window has focus. \
Recognition is sherpa-onnx with the SenseVoice int8 model that the npu-llm \
package already installs (/usr/share/npu-llm/voice/asr), i.e. the same model, \
tokens and decode settings npu-chat uses, so dictation and npu-chat transcribe \
identically. It runs on the CPU: the speech model is small enough that it does \
not need the HTP, and leaving the DSP to the LLM means a dictation can never \
collide with a generation session (see README.md). \
Nothing here is privileged: the daemon runs as the logged-in user, opens the \
microphone through PipeWire's native client API as the logged-in user, and \
inserts text through the shell's Clutter.InputMethod path -- the IBus route, \
the only unprivileged way to put text into a Wayland client (/dev/uinput is \
root-only and keysym injection cannot produce CJK)."

HOMEPAGE = "https://www.quectel.com.cn/docs/quectel-pi/h1"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

# c-api.h is vendored so the build does not depend on npu-llm's sysroot: the
# sherpa-onnx C API is plain C and ABI-stable, and the header has to match the
# library actually installed on the board (1.13.8, the version npu-llm ships).
SRC_URI = " \
    file://pi-voice-inputd.c \
    file://pi-voice-input.c \
    file://pi-voice-hotkey.c \
    file://hotkey-unittest.c \
    file://sherpa-onnx/c-api/c-api.h \
    file://pi-voice-input.service \
    file://pi-voice-hotkey.service \
    file://org.quectel.PiVoiceInput.service \
    file://00-pi-voice-input.gschema.override \
    file://README.md \
    file://gnome-shell-extensions/pi-voice-input@quectel.com \
"

S = "${WORKDIR}"

inherit pkgconfig systemd

# DEPENDS takes *recipe* names, RDEPENDS takes *runtime package* names, and
# for pipewire the two differ: the recipe is "pipewire" while the library
# package that ships libpipewire-0.3.so.0 and the client.conf it needs is
# "libpipewire". DEPENDS="libpipewire" fails with "Nothing PROVIDES
# 'libpipewire'", and RDEPENDS="pipewire" would pull the daemon instead of the
# library. dbus-1 is the session-bus client the daemon and the CLI both use.
# The model and the sherpa-onnx shared objects are NOT build deps -- the C API
# is loaded at runtime out of npu-llm's private directory (see do_install and
# RDEPENDS).
DEPENDS = "dbus pipewire libevdev"

# sherpa-onnx lives in /usr/lib/npu-llm/voice/sherpa_onnx/lib (npu-llm), and the
# model in /usr/share/npu-llm/voice/asr (also npu-llm). The recipe does not
# link against it at build time -- it dlopen()s -- so it is a runtime dependency.
RDEPENDS:${PN} = " \
    npu-llm \
    dbus \
    libpipewire \
"

do_compile() {
	${CC} ${CFLAGS} ${CPPFLAGS} ${LDFLAGS} \
		${S}/pi-voice-inputd.c -o ${B}/pi-voice-inputd \
		$(pkg-config --cflags --libs dbus-1 libpipewire-0.3) -ldl -lm -lpthread
	${CC} ${CFLAGS} ${CPPFLAGS} ${LDFLAGS} \
		${S}/pi-voice-input.c -o ${B}/pi-voice-input \
		$(pkg-config --cflags --libs dbus-1)
	${CC} ${CFLAGS} ${CPPFLAGS} ${LDFLAGS} \
		${S}/pi-voice-hotkey.c -o ${B}/pi-voice-hotkey \
		$(pkg-config --cflags --libs dbus-1 libevdev)
}

# The Ctrl+Q state machine is the part that cannot be checked by eye on a board
# (key repeat, modifiers, hold/release pairing). Build its unit test as a packaged
# tool so it can be re-run on the device after any change:
#     /usr/libexec/pi-voice-input/hotkey-unittest
# It is not part of do_compile: a failing test must not block the image, the
# point is that the check is available where the code runs.
do_compile:append() {
	${CC} ${CFLAGS} ${CPPFLAGS} ${LDFLAGS} \
		-I${S} \
		${S}/hotkey-unittest.c -o ${B}/hotkey-unittest \
		$(pkg-config --cflags --libs dbus-1 libevdev)
}

do_install() {
	install -d ${D}${bindir} ${D}${datadir}/pi-voice-input \
	           ${D}${systemd_user_unitdir} \
	           ${D}${datadir}/dbus-1/services \
	           ${D}${datadir}/gnome-shell/extensions \
	           ${D}${datadir}/doc/${PN} \
	           ${D}${sysconfdir}/dconf/db/local.d

	install -m 0755 ${B}/pi-voice-inputd ${D}${bindir}/pi-voice-inputd
	install -m 0755 ${B}/pi-voice-input ${D}${bindir}/pi-voice-input
	install -m 0755 ${B}/pi-voice-hotkey ${D}${bindir}/pi-voice-hotkey
	install -d ${D}${libexecdir}/pi-voice-input
	install -m 0755 ${B}/hotkey-unittest \
		${D}${libexecdir}/pi-voice-input/hotkey-unittest

	# User unit, not a system unit: the daemon has to live in the desktop
	# session to see that session's bus, PipeWire and microphone.
	install -m 0644 ${S}/pi-voice-input.service \
		${D}${systemd_user_unitdir}/pi-voice-input.service
	install -m 0644 ${S}/pi-voice-hotkey.service \
		${D}${systemd_user_unitdir}/pi-voice-hotkey.service
	# Both units carry WantedBy=graphical-session.target, so "enabled" for a
	# *user* unit means this symlink. It has to be created here and not in
	# pkg_postinst: pkg_postinst returns early when $D is set, which is exactly
	# the image build, so 'systemctl --global enable' never ran and the units
	# stayed disabled on the flashed image (pi-voice-hotkey dead, no
	# push-to-talk). The path mirrors what systemctl --global enable writes, so
	# a later 'systemctl --user disable' still works.
	#
	# graphical-session.target and not default.target: the user manager survives
	# a logout, so default.target is already active when the next login happens
	# and its wants are not re-pulled -- measured on the board, the daemon
	# stayed dead after re-login until the next reboot.
	install -d ${D}${sysconfdir}/systemd/user/graphical-session.target.wants
	ln -sf ${systemd_user_unitdir}/pi-voice-input.service \
		${D}${sysconfdir}/systemd/user/graphical-session.target.wants/pi-voice-input.service
	ln -sf ${systemd_user_unitdir}/pi-voice-hotkey.service \
		${D}${sysconfdir}/systemd/user/graphical-session.target.wants/pi-voice-hotkey.service
	# Session bus activation: the first call from the shell extension or from
	# the Ctrl+Q client starts the daemon, so nothing has to be started at
	# login by hand.
	install -m 0644 ${S}/org.quectel.PiVoiceInput.service \
		${D}${datadir}/dbus-1/services/org.quectel.PiVoiceInput.service

	install -d ${D}${datadir}/gnome-shell/extensions/pi-voice-input@quectel.com
	install -m 0644 \
		${S}/gnome-shell-extensions/pi-voice-input@quectel.com/metadata.json \
		${S}/gnome-shell-extensions/pi-voice-input@quectel.com/extension.js \
		${S}/gnome-shell-extensions/pi-voice-input@quectel.com/stylesheet.css \
		${D}${datadir}/gnome-shell/extensions/pi-voice-input@quectel.com/

	# GNOME 50 ignores any extension that is not listed here, and this image
	# has no user login configuration to write into, so the switch has to be
	# turned on at the system level.  Written as a dconf key file (applied by
	# 'dconf update', which atci's dconf-update.service runs at boot) rather
	# than by shelling out to gsettings: that is the same mechanism the image
	# already uses for its screensaver/session defaults.
	install -m 0644 ${WORKDIR}/00-pi-voice-input.gschema.override \
		${D}${sysconfdir}/dconf/db/local.d/00-pi-voice-input

	install -m 0644 ${S}/pi-voice-inputd.c \
		${D}${datadir}/pi-voice-input/pi-voice-inputd.c
	install -m 0644 ${S}/pi-voice-input.c \
		${D}${datadir}/pi-voice-input/pi-voice-input.c
	install -m 0644 ${S}/pi-voice-hotkey.c \
		${D}${datadir}/pi-voice-input/pi-voice-hotkey.c
	install -m 0644 ${S}/hotkey-unittest.c \
		${D}${datadir}/pi-voice-input/hotkey-unittest.c
	# The unit's Documentation= line points here, and the trade-offs written in
	# it (why no NPU, why /dev/input, why Clutter.InputMethod) are the things a
	# maintainer needs before touching this package.
	install -m 0644 ${S}/README.md ${D}${datadir}/doc/${PN}/README.md
}

FILES:${PN} = " \
    ${bindir}/pi-voice-inputd \
    ${bindir}/pi-voice-input \
    ${bindir}/pi-voice-hotkey \
    ${libexecdir}/pi-voice-input/hotkey-unittest \
    ${datadir}/pi-voice-input \
    ${datadir}/dbus-1/services/org.quectel.PiVoiceInput.service \
    ${datadir}/gnome-shell/extensions/pi-voice-input@quectel.com \
    ${systemd_user_unitdir}/pi-voice-input.service \
    ${systemd_user_unitdir}/pi-voice-hotkey.service \
    ${sysconfdir}/systemd/user/graphical-session.target.wants/pi-voice-input.service \
    ${sysconfdir}/systemd/user/graphical-session.target.wants/pi-voice-hotkey.service \
    ${sysconfdir}/dconf/db/local.d/00-pi-voice-input \
    ${datadir}/doc/${PN} \
"

# The unit is a *user* unit, so it is not in SYSTEMD_SERVICE: that variable is
# for the system manager at boot, where a session service cannot work. Instead
# 'systemctl --global enable' links it into user@.service's wants, and the
# session manager starts it with the desktop.
pkg_postinst:${PN}() {
#!/bin/sh
if [ -n "$D" ]; then
    exit 0
fi
systemctl --global enable pi-voice-input.service pi-voice-hotkey.service >/dev/null 2>&1 || true
# Make the new dconf key file visible to the running session as well (it is
# applied at boot by dconf-update.service for everyone else).
command -v dconf >/dev/null 2>&1 && dconf update >/dev/null 2>&1 || true
exit 0
}

pkg_postrm:${PN}() {
#!/bin/sh
if [ -n "$D" ]; then
    exit 0
fi
systemctl --global disable pi-voice-input.service pi-voice-hotkey.service >/dev/null 2>&1 || true
command -v dconf >/dev/null 2>&1 && dconf update >/dev/null 2>&1 || true
exit 0
}

# The source is installed for the board to read; no debug symbols here.
INSANE_SKIP:${PN} += "already-stripped"
