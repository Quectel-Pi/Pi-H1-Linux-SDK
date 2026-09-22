#!/bin/sh
# Build the Qualcomm msm_vidc VA-API driver (msm_drv_video.so).
#
# Runs on the board itself (gcc + libva-dev are all that is needed) or with any
# aarch64 cross compiler via CC=...  The installed file name matters: libva
# resolves the DRM driver name "msm" straight to msm_drv_video.so, so no
# LIBVA_DRIVER_NAME is required for applications to pick it up.
#
#   ./build.sh              # build msm_drv_video.so in the current directory
#   ./build.sh --install    # build and install into $(DRI_DIR)
#
set -eu

here=$(cd -- "$(dirname -- "$0")" && pwd)
CC=${CC:-gcc}
DRI_DIR=${DRI_DIR:-/usr/lib/aarch64-linux-gnu/dri}
OUT=${OUT:-$PWD/msm_drv_video.so}

# -DVENUS_VAAPI_PROBE_LIMIT must exceed the highest /dev/video index scanned;
# the decoder is /dev/video32 on this board.
$CC -O2 -fPIC -shared -D_GNU_SOURCE -DVENUS_VAAPI_PROBE_LIMIT=64 \
    -I"$here/files/include" -I"$here/files/src" \
    -o "$OUT" "$here"/files/src/*.c -lva -lpthread

echo "built $OUT"
"$CC" --version | head -1 >/dev/null 2>&1 || true

if [ "${1:-}" = "--install" ]; then
    install -m 755 "$OUT" "$DRI_DIR/msm_drv_video.so"
    echo "installed $DRI_DIR/msm_drv_video.so"
    echo "check with: vainfo"
fi
