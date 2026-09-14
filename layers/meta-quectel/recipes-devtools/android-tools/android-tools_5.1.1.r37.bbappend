# meta-quectel patch for android-tools-adbd (5.1.1.r37).
#
# Problem: adbd's reboot_service() calls property_set(ANDROID_RB_PROPERTY,
# "reboot,..."), which needs the Android init property service. This device
# runs native Linux + systemd (no Android init), so property_set() fails,
# reboot_service falls into while(1) pause(), and the adb client hangs until
# transport timeout. This is why `adb reboot` (no `shell`) does nothing here.
#
# Fix: on non-__ANDROID__ builds, run /sbin/reboot directly via system().
# Here /sbin/reboot -> systemctl, so `adb reboot` triggers a real systemd
# reboot (same path `adb shell reboot` already takes). Verified end-to-end
# against the Debian-line adbd binary by adb-pushing it onto the board.
#
# Upstream recipe pins 5.1.1.r37 via PREFERRED_VERSION; we keep that version
# and only patch the one source file. No conf-level changes, no version bump,
# no dependency changes (5.1.1 adbd is self-contained).

# Make the patch in this directory discoverable for both target and
# android-tools-native (the upstream recipe's recipe directory alone does
# not contain it, so pure file:// would fail with "file could not be found").
FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI += "file://0001-adbd-reboot-service-use-sbin-reboot-on-non-Android-Linux.patch;patchdir=system/core"
