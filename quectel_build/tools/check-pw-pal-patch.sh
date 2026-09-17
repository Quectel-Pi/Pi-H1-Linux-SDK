#!/bin/bash
# Verify the pw-pal-plugin patch the way bitbake will: without building.
#
# Two things are checked, and both matter:
#   1. do_patch: quilt/git apply the patch *inside* S (the dir that holds
#      pw-pal-plugin.c), so the diff paths must be relative to S. Getting this
#      wrong only shows up as a bitbake do_patch failure after a long build.
#   2. do_compile: the patched source type-checks against the real cross
#      toolchain and headers this SDK already built (sysroots-components).
#
# S is taken from the recipe itself, not guessed.
# Usage: quectel_build/tools/check-pw-pal-patch.sh [sdk-root]
set -u

SDK=${1:-$(cd "$(dirname "$0")/../.." && pwd)}
PATCH="$SDK/layers/meta-quectel/recipes-quectel/quectel/qcom-pw-pal-plugin/0001-pw-pal-plugin-retry-stream-start-until-ADSP-ready.patch"
BB="$SDK/layers/meta-quectel/recipes-quectel/quectel/qcom-pw-pal-plugin_git.bbappend"
SRC="$SDK/layers/meta-qcom-hwe/recipes-multimedia/audio/qcom-pw-pal-plugin_git.bb"
GITDIR="$SDK/downloads/git2/git.codelinaro.org.clo.le.platform.vendor.qcom-opensource.pipewire-plugin"
C="$SDK/build-qcom-wayland/tmp-glibc/sysroots-components"
GCC="$C/x86_64/gcc-cross-aarch64/usr/bin/aarch64-qcom-linux/aarch64-qcom-linux-gcc"

fail=0
ok()   { echo "PASS  $1"; }
bad()  { echo "FAIL  $1"; fail=1; }
skip() { echo "SKIP  $1"; }
want() { if [ "$2" = "$3" ]; then ok "$1"; else bad "$1 (got '$2', want '$3')"; fi; }

[ -f "$PATCH" ] && ok "patch file present" || bad "patch file present"
grep -qF "file://$(basename "$PATCH")" "$BB" \
    && ok "patch listed in SRC_URI (bbappend)" || bad "patch listed in SRC_URI (bbappend)"

SRCREV=$(sed -n 's/^SRCREV *= *"\(.*\)"/\1/p' "$SRC")
[ -n "$SRCREV" ] || { bad "could not read SRCREV from $(basename "$SRC")"; SRCREV=HEAD; }
echo "      recipe SRCREV: $SRCREV"

# S as the recipe defines it, relative to WORKDIR, and the destsuffix the
# fetcher unpacks to. A local clone of the mirror corresponds to the
# destsuffix directory, so S inside that clone is S minus the destsuffix.
SDEF=$(sed -n 's/^S *= *"\(.*\)"/\1/p' "$SRC")
SDEF=${SDEF#\$\{WORKDIR\}/}
DESTSUFFIX=$(sed -n 's/.*destsuffix=\([^;"]*\).*/\1/p' "$SRC")
if [ -n "$DESTSUFFIX" ]; then
    SDEF=${SDEF#"$DESTSUFFIX"/}
fi
echo "      recipe S = \$WORKDIR/$DESTSUFFIX/$SDEF"

WORK=$(mktemp -d "${TMPDIR:-/tmp}/check-pw-pal-patch.XXXXXX")
trap 'rm -rf "$WORK"' EXIT
SRC_DIR="$WORK/src"

if [ -d "$GITDIR" ] && git clone -q --no-checkout "$GITDIR" "$SRC_DIR" 2>/dev/null &&
   git -C "$SRC_DIR" checkout -q "$SRCREV" 2>/dev/null; then
    ok "cloned pinned source from downloads/git2"
else
    skip "no local source mirror at $GITDIR (run a build to fetch it)"
    SRC_DIR=""
fi

if [ -n "$SRC_DIR" ]; then
    S="$SRC_DIR/$SDEF"
    [ -f "$S/pw-pal-plugin.c" ] || bad "S contains pw-pal-plugin.c ($S)"

    # --- 1. do_patch, exactly where and how bitbake runs it ----------------
    # bitbake's quilt wrapper shells out to patch(1), which resolves hunks from
    # the ---/+++ lines (-p1 inside S). Use patch(1) too: git apply would follow
    # the "diff --git" header instead and pass patches that do_patch rejects.
    if [ -f "$S/pw-pal-plugin.c" ]; then
        if ( cd "$S" && patch -p1 --dry-run --forward < "$PATCH" ) >"$WORK/apply.out" 2>&1; then
            ok "applies inside S with patch -p1 (as quilt/do_patch will)"
            ( cd "$S" && patch -p1 --forward < "$PATCH" ) >/dev/null 2>&1
        else
            bad "applies inside S with patch -p1 (as quilt/do_patch will)"
            head -6 "$WORK/apply.out" | sed 's/^/      /'
            echo "      hint: patch paths must be relative to S, not to the repo root"
            grep -m2 '^--- a/' "$PATCH" | sed 's/^/      /'
        fi
        # The paths S expects, independent of any patch tool's header preference.
        want_rel=$(grep -m1 '^+++ b/' "$PATCH" | sed 's|^+++ b/||')
        [ -f "$S/$want_rel" ] && ok "patch target exists under S: $want_rel" \
            || bad "patch target exists under S (looked for $want_rel)"
        # And the git header must agree, since git-based appliers read it first.
        git_rel=$(grep -m1 '^diff --git ' "$PATCH" | sed 's|^diff --git a/||; s| b/.*||')
        want "diff-git header agrees with ---/+++ paths" "$git_rel" "$want_rel"
    fi

    # --- 2. do_compile ------------------------------------------------------
    if [ -x "$GCC" ] && [ -f "$S/pw-pal-plugin.c" ]; then
        SR="$WORK/sysroot"; mkdir -p "$SR/usr/include"
        for comp in glibc linux-libc-headers gcc-runtime pipewire qcom-pal-headers \
                    qcom-agm qcom-pal qcom-args wireplumber alsa-lib; do
            d="$C/armv8-2a/$comp/usr/include"
            [ -d "$d" ] || continue
            for e in "$d"/*; do
                n=$(basename "$e")
                [ -e "$SR/usr/include/$n" ] || ln -s "$e" "$SR/usr/include/$n"
            done
        done
        if "$GCC" --sysroot="$SR" -fsyntax-only -Wall \
                -I "$SR/usr/include/spa-0.2" \
                -I "$SR/usr/include/pipewire-0.3" \
                -I "$SR/usr/include/pal" \
                "$S/pw-pal-plugin.c" > "$WORK/cc.log" 2>&1; then
            ok "patched source compiles (cross gcc -fsyntax-only, $(grep -c 'warning:' "$WORK/cc.log") warnings, 0 errors)"
        else
            bad "patched source compiles:"
            grep -m5 'error:' "$WORK/cc.log" | sed 's/^/      /'
        fi
    else
        skip "cross gcc unavailable (needs a prior build)"
    fi
fi

echo
if [ $fail -eq 0 ]; then echo "ALL CHECKS PASSED"; else echo "SOME CHECKS FAILED"; fi
exit $fail
