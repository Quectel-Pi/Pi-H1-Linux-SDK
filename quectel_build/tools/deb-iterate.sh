#!/bin/bash
# Iterate one or more Yocto components and publish the result to the apt
# repository, in one command.
#
#   ./quectel_build/tools/deb-iterate.sh msm-va-driver
#   ./quectel_build/tools/deb-iterate.sh virtual/kernel
#   ./quectel_build/tools/deb-iterate.sh msm-va-driver linux-firmware
#
# What it does, per component:
#   1. bump that component's package revision in
#      layers/meta-quectel/conf/deb-track/versions.inc  (r0 -> r1 -> r2 ...)
#   2. rebuild only that recipe (`-c package_write_deb -f`)
#   3. verify the new deb really carries the new version
# then, once for the whole batch:
#   4. republish the apt repository, keeping older versions so downgrade works
#
# Why step 1 is not optional (and why it is automated): apt compares package
# versions, never file contents.  A rebuilt deb with the same version is
# "already the newest version" to apt and the board keeps the old file.
#
# Why this does not rebuild the image: the board upgrades from the repository,
# not from a new system.img.  Rebuild the image only when shipping a full
# firmware release (buildpackage).
set -u

TOPDIR="${TOPDIR:-$(pwd)}"
VERSIONS="${TOPDIR}/layers/meta-quectel/conf/deb-track/versions.inc"
PUBLISH="${TOPDIR}/quectel_build/tools/deb-publish.sh"
DEPLOY="${TOPDIR}/build-qcom-wayland/tmp-glibc/deploy/deb"
BUILD_DIR="${TOPDIR}/build-qcom-wayland"

die()  { echo "[deb-iterate] ERROR: $*" >&2; exit 1; }
note() { echo "[deb-iterate] $*"; }

[ -f "$VERSIONS" ] || die "$VERSIONS not found (is TOPDIR correct?)"
[ -x "$PUBLISH" ]  || die "$PUBLISH not found or not executable"
[ $# -ge 1 ] || { sed -n '2,26p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }

# bitbake must run inside the build dir with the OE environment.
if ! command -v bitbake >/dev/null 2>&1; then
    note "sourcing oe-init-build-env ..."
    # shellcheck disable=SC1091
    ( cd "$BUILD_DIR" && source "${TOPDIR}/layers/poky/oe-init-build-env" . >/dev/null ) 2>/dev/null
    if [ -f "${BUILD_DIR}/oe-init-build-env" ]; then
        :
    fi
    # shellcheck disable=SC1091
    source "${TOPDIR}/layers/poky/oe-init-build-env" "$BUILD_DIR" >/dev/null 2>&1
    command -v bitbake >/dev/null 2>&1 || die "bitbake not found after sourcing oe-init-build-env"
fi

# --- map a recipe/target name to the PN used in versions.inc ---------------
# versions.inc keys on the recipe's PN (file name without version), not on
# virtual/ targets, so resolve those here.
resolve_pn() {
    case "$1" in
        virtual/kernel) echo "linux-qcom-custom" ;;
        *)              echo "$1" ;;
    esac
}

# --- bump one entry in versions.inc ---------------------------------------
# Kept as an in-place edit of a single line so the file stays reviewable.
bump_pr() {
    local pn="$1"
    python3 - "$VERSIONS" "$pn" <<'PYEOF'
import re, sys

path, pn = sys.argv[1], sys.argv[2]
key = 'DEB_PR:pn-%s' % pn
text = open(path).read()
m = re.search(r'^%s = "r(\d+)"$' % re.escape(key), text, re.M)
if m:
    old = int(m.group(1))
    new_line = '%s = "r%d"' % (key, old + 1)
    text = text[:m.start()] + new_line + text[m.end():]
    print('bumped %s: r%d -> r%d' % (pn, old, old + 1))
else:
    # No entry yet: start at r1 so the first iteration is an upgrade relative to
    # the implicit r0 that the recipe falls back to.
    text = text.rstrip('\n') + '\n%s = "r1"\n' % key
    print('added %s = r1 (was implicit r0)' % pn)
open(path, 'w').write(text)
PYEOF
}

# --- current version of a PN inside versions.inc --------------------------
current_pr() {
    sed -n "s/^DEB_PR:pn-$1 = \"\(r[0-9]*\)\"$/\1/p" "$VERSIONS" | tail -1
}

# --- find the newest deb for a PN in deploy/deb ---------------------------
newest_deb() {
    local pn="$1"
    find "$DEPLOY" -maxdepth 2 -name "${pn}_*.deb" ! -name '*-dbg_*' \
        ! -name '*-dev_*' ! -name '*-staticdev_*' ! -name '*-src_*' \
        ! -name '*-doc_*' -printf '%T@ %p\n' 2>/dev/null \
        | sort -n | tail -1 | cut -d' ' -f2-
}

declare -a PWNS=()
for target in "$@"; do
    pn="$(resolve_pn "$target")"
    note "=== $target (PN $pn) ==="

    old_pr="$(current_pr "$pn")"
    bump_pr "$pn" || die "could not bump PR for $pn"
    new_pr="$(current_pr "$pn")"
    [ -n "$new_pr" ] || die "PR for $pn is empty after bump"

    note "rebuilding $target ..."
    if ! bitbake "$target" -c package_write_deb -f; then
        # Roll the version back so a failed build does not leave a gap.
        python3 - "$VERSIONS" "$pn" "$old_pr" <<'PYEOF'
import re, sys
path, pn, old = sys.argv[1], sys.argv[2], sys.argv[3]
text = open(path).read()
key = 'DEB_PR:pn-%s' % pn
text = re.sub(r'^%s = "r\d+"$' % re.escape(key), '%s = "%s"' % (key, old), text, flags=re.M)
open(path, 'w').write(text)
PYEOF
        die "bitbake $target failed; $pn rolled back to ${old_pr:-r0}"
    fi

    # The rebuilt deb must actually carry the new version, otherwise the whole
    # point of the bump is lost.
    deb="$(newest_deb "$pn")"
    [ -n "$deb" ] || die "no ${pn}_*.deb in $DEPLOY after the build"
    ver="$(dpkg-deb -f "$deb" Version 2>/dev/null)"
    case "$ver" in
        *"-$new_pr"|*"-$new_pr:"*|"$new_pr")
            note "ok: $(basename "$deb") -> Version $ver" ;;
        *)
            die "$(basename "$deb") reports Version '$ver' but versions.inc says '$new_pr'.
     The recipe probably does not use PR = \"\${@d.getVar('DEB_PR') or 'r0'}\".
     Check: grep -n 'PR = ' <recipe>" ;;
    esac
    PWNS+=("$pn")
done

note "=== publishing (history kept, downgrade stays possible) ==="
if ! "$PUBLISH" --from-lists; then
    die "publishing failed; the rebuilt debs are still in $DEPLOY"
fi

cat <<EOF

[deb-iterate] done: ${PWNS[*]}

Next:
  upload  quectel_build/deb/  to the HTTP server (whole directory)
  on the board:
      apt update
      apt-cache policy <pkg>            # Candidate must be the new version
      apt install --only-upgrade <pkg>
      dpkg -l <pkg> | tail -1           # confirm
  downgrade (older versions are still published):
      apt install <pkg>=<old-version>
EOF
