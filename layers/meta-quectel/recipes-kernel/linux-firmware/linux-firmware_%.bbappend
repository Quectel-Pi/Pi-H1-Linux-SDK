# Package revision for linux-firmware (and all its split packages, e.g.
# linux-firmware-qcom-qcm6490-audio) comes from the central table in
# layers/meta-quectel/conf/deb-track/versions.inc, bumped by
# quectel_build/tools/deb-iterate.sh.
#
# The version currently shipping looks like "1:20251125-r0": epoch 1, the
# upstream snapshot date, and the PR.  Raising PR gives "1:20251125-r1" which
# apt accepts as newer, so in-place firmware updates work without touching the
# snapshot date.
#
# Keep the epoch.  "1:..." always sorts above any version without one, so a
# repository entry that drops it (e.g. plain "20251126-r0") is treated as a
# DOWNGRADE and apt silently skips it.
PR = "${@d.getVar('DEB_PR') or 'r0'}"
