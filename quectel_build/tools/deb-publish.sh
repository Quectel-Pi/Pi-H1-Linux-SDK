#!/bin/bash
# Publish the debs tracked by deb-track into an apt-repository layout.
#
# Two data sources:
#
#   default        read ${WORKDIR}/deb-track/PUBLISH, written by deb_track_install
#                  during do_image.  This is the exact set that went into the
#                  image, so the repository and the firmware always agree.
#                  Used by buildpackage.
#
#   --from-lists   resolve conf/deb-track/*.txt against deploy/deb right now.
#                  Needed for iteration: when you rebuild only one recipe
#                  (`bitbake msm-va-driver`), PUBLISH still lists the old
#                  version, because it is only rewritten by do_image.  This mode
#                  picks up the freshly built deb without rebuilding the image.
#
# Package selection (both modes): the package lists plus their dependency
# closure resolved from deploy/deb, minus DEB_TRACK_CLOSURE_DENY.  Never the
# whole deploy/deb tree.
#
# Output (default quectel_build/deb):
#   Packages, Packages.gz, Release      apt metadata, ready for `apt update`
#   *.deb                               the packages themselves
#   DISTRIBUTION                        one-line description for humans
#
# Serving it over HTTP:
#   cp -r quectel_build/deb /var/www/html/quectel
#   # on the board:
#   echo "deb [trusted=yes] http://<host>/quectel ./" >/etc/apt/sources.list.d/quectel.list
#   apt update && apt install <package>
#
# `[trusted=yes]` is needed because these debs are not signed.  Add a
# Release.gpg / InRelease later if the repository goes public.
set -u

usage() {
    # Quoted heredoc: the text contains backticks and $TOPDIR, which must be
    # shown literally rather than expanded.
    cat <<'EOF'
usage: deb-publish.sh [options] [OUT_DIR]

  --from-lists    Resolve conf/deb-track/*.txt now instead of reading the
                  PUBLISH manifest.  Use this after rebuilding a single recipe.
  --replace-all   Delete older *.deb from OUT_DIR before publishing.  The
                  default KEEPS them, so 'apt install <pkg>=<oldver>' works
                  (downgrade).  Same-version files are overwritten, so the
                  directory only grows when a version actually changes.
  -h, --help      this text

  OUT_DIR defaults to $TOPDIR/quectel_build/deb.
EOF
}

TOPDIR="${TOPDIR:-$(pwd)}"
OUT_DIR="${TOPDIR}/quectel_build/deb"
FROM_LISTS=0
# Downgrade support is the default: keep every version ever published so apt
# still sees the old ones.  Same-version debs are simply overwritten, so this
# does not grow on rebuilds that did not bump a version.
KEEP_HISTORY=1

while [ $# -gt 0 ]; do
    case "$1" in
        --from-lists)   FROM_LISTS=1 ;;
        --replace-all)  KEEP_HISTORY=0 ;;
        --keep-history) KEEP_HISTORY=1 ;;   # still accepted, now the default
        -h|--help)      usage; exit 0 ;;
        --*)            echo "[deb-publish] unknown option: $1" >&2; usage; exit 2 ;;
        # Backward compatibility: buildpackage passes OUT_DIR positionally.
        *)              OUT_DIR="$1" ;;
    esac
    shift
done

CLASS="${TOPDIR}/layers/meta-quectel/classes/deb-track.bbclass"
LISTS_DIR="${TOPDIR}/layers/meta-quectel/conf/deb-track"
DEPLOY="${TOPDIR}/build-qcom-wayland/tmp-glibc/deploy/deb"

if [ ! -f "$CLASS" ]; then
    echo "[deb-publish] ERROR: $CLASS not found (is TOPDIR correct?)"
    exit 1
fi
if [ ! -d "$DEPLOY" ]; then
    echo "[deb-publish] ERROR: $DEPLOY not found (build the image or the recipes first)"
    exit 1
fi

MANIFEST=""
TMP_MANIFEST=""

if [ "$FROM_LISTS" -eq 1 ]; then
    # Resolve the lists ourselves.  The closure/deny logic is imported straight
    # from the bbclass so the two can never drift apart.
    TMP_MANIFEST="$(mktemp)"
    python3 - "$TOPDIR" "$CLASS" "$LISTS_DIR" "$DEPLOY" "$TMP_MANIFEST" <<'PYEOF' || exit 1
import glob, os, re, shutil, sys, subprocess

topdir, classfile, listsdir, deploy, outfile = sys.argv[1:6]

src = open(classfile).read()
# Everything between the first helper and the task bodies is plain python and
# safe to exec: _deb_track_control() and _deb_track_closure().
start = src.index('def _deb_track_control(')
end = src.index('# task bodies') - len('# ------')
ns = {}
exec(compile(src[start:end], classfile, 'exec'), ns)
control = ns['_deb_track_control']
closure = ns['_deb_track_closure']

m = re.search(r'DEB_TRACK_CLOSURE_DENY \?\?= " \\\n(.*?)"', src, re.S)
deny = m.group(1).replace('\\', '').split() if m else []

# Names the Debian/Ubuntu base already ships.  This file lives in the same
# directory but is a *criterion*, not a package list: feeding it to the
# resolver would ask for a distro package that deploy/deb never builds.
DISTRO_OWNED_FILE = os.path.join(listsdir, 'distro-owned.txt')
distro_owned = set()
if os.path.exists(DISTRO_OWNED_FILE):
    for line in open(DISTRO_OWNED_FILE):
        line = line.split('#', 1)[0].strip()
        if line:
            distro_owned.add(line.split()[0])

# Authoritative criterion: paths the distro base already owns.  Package names
# miss cases where Yocto and the distro name the same library differently.
import gzip
DISTRO_PATHS_FILE = os.path.join(listsdir, 'distro-owned-paths.txt.gz')
distro_paths = set()
if os.path.exists(DISTRO_PATHS_FILE):
    with gzip.open(DISTRO_PATHS_FILE, 'rt', errors='replace') as fh:
        for line in fh:
            line = line.strip()
            if line.startswith('/'):
                distro_paths.add(line)

# dpkg-deb: prefer the native sysroot (matches what do_image uses), fall back
# to the host copy.
dpkg_deb = None
for cand in glob.glob(os.path.join(
        topdir, 'build-qcom-wayland/tmp-glibc/work/*/*/*/recipe-sysroot-native/usr/bin/dpkg-deb')):
    if os.access(cand, os.X_OK):
        dpkg_deb = cand
        break
if not dpkg_deb:
    dpkg_deb = shutil.which('dpkg-deb')
if not dpkg_deb:
    sys.exit('[deb-publish] ERROR: dpkg-deb not found')
bindir = os.path.dirname(dpkg_deb)

archs = ['all', 'armv8-2a', 'qcm6490', 'qcm6490_idp']
variants = ('-dbg_', '-dev_', '-staticdev_', '-src_', '-doc_')

resolved = []
missing = []
# Everything under listsdir except the criterion files is a package list.
skip_files = {'distro-owned.txt', 'distro-owned-paths.txt.gz'}
for name in sorted(os.listdir(listsdir)):
    if not name.endswith('.txt') or name in skip_files:
        continue
    for line in open(os.path.join(listsdir, name)):
        line = line.split('#', 1)[0].strip()
        if not line:
            continue
        hits = set()
        for a in archs:
            hits.update(glob.glob(os.path.join(deploy, a, line + '_*.deb')))
            if glob.has_magic(line):
                hits.update(glob.glob(os.path.join(deploy, a, line + '-*_*.deb')))
        hits = sorted(h for h in hits
                      if not any(os.path.basename(h).startswith(line + v) for v in variants))
        if not hits:
            missing.append('%s (%s)' % (line, name))
            continue
        resolved.extend(hits if glob.has_magic(line) else hits[-1:])

if missing:
    sys.stderr.write('[deb-publish] ERROR: no .deb for: %s\n' % ', '.join(missing))
    sys.exit(1)

closure_debs = closure(resolved, archs, deploy, bindir, deny=deny,
                      distro_owned=distro_owned, distro_paths=distro_paths)

# Publish the rewritten copies, not the Yocto originals.
#
# do_image leaves the deb it actually installed in
# WORKDIR/deb-track/debs/<name>: same payload, but with the control file
# normalised (Version git-r0 -> 0.git-r0, dangling dependencies removed,
# unmeetable version constraints dropped).  Copying straight out of deploy/deb
# would hand apt a package whose Version dpkg cannot even parse - that is what
# `dpkg-deb -f <original> Version` failing with "version number does not start
# with digit" looks like.  It would also put the dependency problems back into
# the repository, so a device installing from it would break apt-get check.
staged_dir = None
for root, dirs, files in os.walk(os.path.join(topdir, 'build-qcom-wayland',
                                              'tmp-glibc', 'work')):
    cand = os.path.join(root, 'deb-track', 'debs')
    if os.path.isdir(cand) and any(f.endswith('.deb') for f in os.listdir(cand)):
        staged_dir = cand
        break

with open(outfile, 'w') as fh:
    rewritten = 0
    for deb in closure_debs:
        name = os.path.basename(deb)
        src = deb
        if staged_dir:
            cand = os.path.join(staged_dir, name)
            if os.path.exists(cand):
                src = cand
                rewritten += 1
        fh.write('%s\t%s\n' % (src, name))
sys.stderr.write('[deb-publish] resolved %d package(s) from lists '
                 '(%d distro names, %d distro paths left alone)\n'
                 % (len(closure_debs), len(distro_owned), len(distro_paths)))
if staged_dir:
    sys.stderr.write('[deb-publish] %d/%d taken from the normalised copies in '
                     '%s\n' % (rewritten, len(closure_debs), staged_dir))
    if rewritten != len(closure_debs):
        sys.stderr.write('[deb-publish] WARNING: %d package(s) have no normalised '
                         'copy and will be published straight from deploy/deb; '
                         'run do_image first\n'
                         % (len(closure_debs) - rewritten))
else:
    sys.stderr.write('[deb-publish] WARNING: no deb-track/debs directory found, '
                     'publishing the Yocto originals - Version may be '
                     'unparseable and dependencies unpruned.  Run the image '
                     'build first.\n')
PYEOF
    MANIFEST="$TMP_MANIFEST"
else
    # deb-track leaves its payload listing in the image recipe's WORKDIR.
    for d in "${TOPDIR}"/build-qcom-wayland/tmp-glibc/work/*/quecpi-image/*/deb-track; do
        [ -f "$d/PUBLISH" ] && MANIFEST="$d/PUBLISH"
    done
    if [ -z "$MANIFEST" ]; then
        echo "[deb-publish] no PUBLISH manifest found under:"
        echo "              ${TOPDIR}/build-qcom-wayland/tmp-glibc/work/*/quecpi-image/*/deb-track"
        echo "              build quecpi-image first, or use --from-lists"
        exit 0
    fi
fi
trap '[ -n "$TMP_MANIFEST" ] && rm -f "$TMP_MANIFEST"' EXIT

echo "[deb-publish] manifest : $MANIFEST"
echo "[deb-publish] output   : $OUT_DIR"

# apt-ftparchive: prefer the host one (apt-utils) because the Yocto native copy
# emits a harmless but noisy 'E: Errors apply to file ./Packages' when its
# apt.conf.d is absent; both produce byte-identical output (verified).  The
# native sysroot copy is the fallback so the SDK works on hosts without
# apt-utils.
FTPARCHIVE="$(command -v apt-ftparchive 2>/dev/null || true)"
FTPARCHIVE_SRC="host"
if [ -z "$FTPARCHIVE" ]; then
    for f in $(find "${TOPDIR}/build-qcom-wayland/tmp-glibc" \
            -path "*recipe-sysroot-native/usr/bin/apt-ftparchive" -type f 2>/dev/null); do
        FTPARCHIVE="$f"
        FTPARCHIVE_SRC="native sysroot"
        break
    done
fi
if [ -z "$FTPARCHIVE" ]; then
    echo "[deb-publish] ERROR: apt-ftparchive not found"
    echo "              install apt-utils on the host, or build quecpi-image first"
    exit 1
fi
echo "[deb-publish] ftparchive: $FTPARCHIVE ($FTPARCHIVE_SRC)"

mkdir -p "$OUT_DIR" || exit 1

if [ "$KEEP_HISTORY" -eq 1 ]; then
    echo "[deb-publish] keeping older *.deb in $OUT_DIR (downgrade stays possible)"
else
    # Replace the previously published set so a rebuild never leaves stale debs
    # behind that apt would still offer.
    find "$OUT_DIR" -maxdepth 1 -name '*.deb' -delete
fi
rm -f "$OUT_DIR/Packages" "$OUT_DIR/Packages.gz" "$OUT_DIR/Release"

n=0
# Names copied this run, to report packages that are in the directory but no
# longer produced by the lists (they stay visible to apt while history is kept).
: > "${OUT_DIR}/.published.tmp"
while IFS=$'\t' read -r src name; do
    [ -f "$src" ] || continue
    cp -f "$src" "$OUT_DIR/$name" || exit 1
    echo "$name" >> "${OUT_DIR}/.published.tmp"
    n=$((n + 1))
done < "$MANIFEST"

# With history kept, a package that was dropped from the lists (or whose name
# changed, e.g. after a DISTRO_VERSION bump) still sits in the directory and apt
# keeps offering it.  Say so instead of leaving it a mystery.
if [ "$KEEP_HISTORY" -eq 1 ]; then
    stale=$(find "$OUT_DIR" -maxdepth 1 -name '*.deb' -printf '%f\n' \
            | grep -vxF -f "${OUT_DIR}/.published.tmp" 2>/dev/null | wc -l)
    if [ "$stale" -gt 0 ]; then
        echo "[deb-publish] note: $stale deb(s) in $OUT_DIR are not produced by the"
        echo "              current lists (kept for downgrade). Examples:"
        find "$OUT_DIR" -maxdepth 1 -name '*.deb' -printf '%f\n' \
            | grep -vxF -f "${OUT_DIR}/.published.tmp" 2>/dev/null \
            | head -5 | sed 's/^/[deb-publish]        /'
        echo "              remove them with: --replace-all"
    fi
fi
rm -f "${OUT_DIR}/.published.tmp"

if [ "$n" -eq 0 ]; then
    echo "[deb-publish] ERROR: manifest is empty"
    exit 1
fi

# Build the apt index.  Run inside OUT_DIR so the paths are relative ("./").
# With --keep-history this scans every deb present, so older versions stay
# visible to apt.
(
    cd "$OUT_DIR" || exit 1
    "$FTPARCHIVE" packages . > Packages.tmp || exit 1
    mv Packages.tmp Packages
    gzip -9 -n -c Packages > Packages.gz
    "$FTPARCHIVE" -o APT::FTPArchive::Release::Origin="Quectel PI" \
                  -o APT::FTPArchive::Release::Label="Quectel PI BSP" \
                  -o APT::FTPArchive::Release::Suite="stable" \
                  -o APT::FTPArchive::Release::Codename="quectel" \
                  -o APT::FTPArchive::Release::Architectures="arm64 all" \
                  -o APT::FTPArchive::Release::Components="main" \
                  release . > Release.tmp || exit 1
    mv Release.tmp Release
)

# How many deb files are actually in the output dir (differs from $n only with
# --keep-history).
ndeb=$(find "$OUT_DIR" -maxdepth 1 -name '*.deb' | wc -l)

{
    echo "Quectel PI BSP packages"
    echo "generated : $(date -u '+%Y-%m-%d %H:%M:%S UTC')"
    echo "packages  : $ndeb"
    echo "source    : $([ "$FROM_LISTS" -eq 1 ] && echo 'deb-track lists (live)' || echo 'deb-track PUBLISH manifest')"
    echo "history   : $([ "$KEEP_HISTORY" -eq 1 ] && echo 'kept' || echo 'replaced each run')"
} > "$OUT_DIR/DISTRIBUTION"

# Verify the index is self-consistent; a truncated Packages would make apt
# silently see no packages at all, and a stale index would offer files that are
# not there.
npkg=$(grep -c '^Package: ' "$OUT_DIR/Packages" 2>/dev/null || echo 0)
if [ "$npkg" -eq 0 ]; then
    echo "[deb-publish] ERROR: generated Packages contains no package stanzas"
    exit 1
fi
if [ "$npkg" -ne "$ndeb" ]; then
    echo "[deb-publish] ERROR: Packages lists $npkg package(s) but the directory"
    echo "              holds $ndeb deb file(s)"
    exit 1
fi
if ! gzip -dc "$OUT_DIR/Packages.gz" | cmp -s - "$OUT_DIR/Packages"; then
    echo "[deb-publish] ERROR: Packages.gz does not match Packages"
    exit 1
fi
# Every Filename: in the index must exist on disk.
while read -r f; do
    [ -f "$OUT_DIR/${f#./}" ] || { echo "[deb-publish] ERROR: index refers to missing file $f"; exit 1; }
done < <(sed -n 's/^Filename: //p' "$OUT_DIR/Packages")

echo "[deb-publish] copied $n from manifest, $ndeb package(s) in the repo"
echo "[deb-publish] sizes:"
du -sh "$OUT_DIR" 2>/dev/null | sed 's/^/[deb-publish]   /'
echo "[deb-publish] to serve:"
echo "    cp -r $OUT_DIR <webroot>/quectel"
echo "  on the board:"
echo "    echo \"deb [trusted=yes] http://<host>/quectel ./\" > /etc/apt/sources.list.d/quectel.list"
echo "    apt update"
