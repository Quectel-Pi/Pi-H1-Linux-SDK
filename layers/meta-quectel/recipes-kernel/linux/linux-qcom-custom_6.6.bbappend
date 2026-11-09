# Package revision for the kernel comes from the central table in
# layers/meta-quectel/conf/deb-track/versions.inc, bumped automatically by
# quectel_build/tools/deb-iterate.sh.
#
# Why an override is needed at all: `apt upgrade` on the board compares package
# versions, not file contents.  Rebuilding the kernel with a changed driver but
# the same PR produces debs with identical versions, so apt reports "already the
# newest version" and the board keeps running the old module.
#
# Bumping PR here re-versions the whole kernel package set at once:
#   kernel-6.6.116-qli-1.7-ver.1.1           6.6-r0 -> 6.6-r1
#   kernel-image-*                           ...
#   kernel-module-*  (684 packages)          ...
# The package NAMES do not change, so this is a normal in-place upgrade and the
# old modules get overwritten.  (Changing DISTRO_VERSION instead would change
# the names - see .pi/skills/deb-iterate/SKILL.md.)
PR = "${@d.getVar('DEB_PR') or 'r0'}"
