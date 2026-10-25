#!/bin/sh
# Seed BitBake's DL_DIR with the qwen35-npu-model assets without touching the
# network.
#
#   ./seed-dl-dir.sh [DL_DIR]
#
# DL_DIR defaults to $DL_DIR, then to <this dir>/../../../../downloads.
#
# Why this exists
# ---------------
# The 15 assets are 2.05 GB of build products, so they are not in git.  They live
# in a GitHub Release (QWEN35_QNN_RELEASE_REPO/TAG) and do_fetch pulls them from
# there.  On a machine that has the release staging directory but no internet --
# an air-gapped builder, or a checkout made before the release was published --
# this script copies them out of that directory into DL_DIR instead.  It reads
# the file list and every checksum out of the recipe, so it never needs editing
# when the release changes.
#
# Local staging directory (in the repository checkout, git-ignored)
# ----------------------------------------------------------------
#   <repo>/log/models/<basename>
# which is exactly the layout GitHub Release needs for the upload; see
# log/models/README-upload.md.  Override with QWEN35_ASSET_STAGING=<dir>.
#
# It writes the .done stamp as well, not just the file: do_fetch uses
# verify_donestamp() to decide whether a cached file is current, and a bare file
# with no stamp is re-downloaded even when its sha256 already matches.  The stamp
# is the pickled checksum dict BitBake writes itself (md5/sha1/sha256/sha384/
# sha512), so a seeded DL_DIR is indistinguishable from a downloaded one.
#
# Exit status: 0 = every asset is in DL_DIR and verified, 1 = something is
# missing or does not match its sha256.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
bb=$here/qwen35-npu-model_1.0.bb

# <this dir> is layers/meta-quectel/recipes-quectel/qwen35-npu-model
repo_root=$(cd "$here/../../../.." && pwd)
dl=${1:-${DL_DIR:-$repo_root/downloads}}
staging=${QWEN35_ASSET_STAGING:-$repo_root/log/models}

mkdir -p "$dl"
echo "DL_DIR : $dl"
echo "staging: $staging"

if [ ! -d "$staging" ]; then
	echo "" >&2
	echo "FAILED: staging directory not found: $staging" >&2
	echo "  The release assets have to be present as flat files named after their" >&2
	echo "  basename.  Either populate it (see log/models/README-upload.md) or point" >&2
	echo "  QWEN35_ASSET_STAGING at the directory that has them, or just let" >&2
	echo "  bitbake fetch them from the GitHub Release." >&2
	exit 1
fi

# label:path:group:sha256 -- keep only the rows, drop the line continuations.
rows=$(sed -n '/^QWEN35_QNN_ARTIFACTS = "/,/^"$/p' "$bb" | tr -d '\\' |
	grep -o '[a-z0-9_]*:[^ ]*:[a-z0-9]*:[0-9a-f]\{64\}')
[ -n "$rows" ] || { echo "cannot read QWEN35_QNN_ARTIFACTS from $bb" >&2; exit 1; }

# Only the rows whose group is enabled; vision is on by default but can be off.
groups=$(sed -n 's/^QWEN35_QNN_GROUPS ??= "\(.*\)"/\1/p' "$bb")
case "$groups" in
*'${@'*)
	# The default expands an inline ${@ ... }; vision on is the only case that
	# matters here and it is what the line evaluates to with the default.
	groups="2k vision"
	;;
esac

n_ok=0
n_skip=0
n_bad=0
for row in $rows; do
	path=$(echo "$row" | cut -d: -f2)
	group=$(echo "$row" | cut -d: -f3)
	sha=$(echo "$row" | cut -d: -f4)
	name=${path##*/}

	case " $groups " in
	*" $group "*) ;;
	*)
		echo "skip    $name (group '$group' is not in QWEN35_QNN_GROUPS)"
		n_skip=$((n_skip + 1))
		continue
		;;
	esac

	if [ ! -f "$staging/$name" ]; then
		echo "MISSING $name (expected $staging/$name)"
		n_bad=$((n_bad + 1))
		continue
	fi
	got=$(sha256sum "$staging/$name" | cut -d' ' -f1)
	if [ "$got" != "$sha" ]; then
		echo "BAD     $name (sha256 $got != $sha)"
		n_bad=$((n_bad + 1))
		continue
	fi

	# Copy only when the DL_DIR copy is absent or different; then (re)write the
	# stamp so bitbake treats it as a completed download.
	if [ -f "$dl/$name" ] && [ "$(sha256sum "$dl/$name" | cut -d' ' -f1)" = "$sha" ]; then
		echo "cached  $name"
	else
		cp -f "$staging/$name" "$dl/$name.part"
		mv -f "$dl/$name.part" "$dl/$name"
		echo "seeded  $name"
	fi
	python3 - "$dl/$name" "$dl/$name.done" <<'PY'
import hashlib, pickle, sys
src, stamp = sys.argv[1], sys.argv[2]
data = memoryview(open(src, 'rb').read())
sums = {a: hashlib.new(a, data).hexdigest()
        for a in ('md5', 'sha1', 'sha256', 'sha384', 'sha512')}
with open(stamp, 'wb') as fh:
    pickle.dump(sums, fh, protocol=2)
PY
	n_ok=$((n_ok + 1))
done

echo ""
echo "seeded $n_ok, skipped $n_skip, failed $n_bad"
[ "$n_bad" -eq 0 ] || exit 1
echo "do_fetch now needs no network for qwen35-npu-model"
