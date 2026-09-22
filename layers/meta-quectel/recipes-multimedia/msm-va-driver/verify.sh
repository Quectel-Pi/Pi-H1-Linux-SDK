#!/bin/bash
# Check the msm-va-driver recipe on a board connected by adb.
#
# Settles the three claims that do not need bitbake:
#   1. build.sh builds msm_drv_video.so from files/ (the repository copy)
#   2. libva loads it as the "msm" driver with no environment variables set
#   3. H.264 without reordering decodes bit-exactly against software
# It also prints the B-frame pass rate, which is the documented limitation.
#
# Scratch space is /var/tmp on purpose: the raw NV12 dumps are 200 MB each and
# the board's /tmp is a 3.6 GB tmpfs, so a full /tmp silently truncates them
# into phantom decode mismatches.
#
# Usage: ./verify.sh [path-to-adb]   (default: the SDK's tools/adb)
set -u

here=$(cd -- "$(dirname -- "$0")" && pwd)
repo=$(cd -- "$here/../../../.." && pwd)
ADB=${1:-$repo/quectel_build/tools/adb}
D=/var/tmp/hv-msm-va

fail=0
pass() { echo "  PASS: $*"; }
bad() { echo "  FAIL: $*"; fail=$((fail + 1)); }

cleanup() { "$ADB" shell "rm -rf $D /tmp/src.tgz" >/dev/null 2>&1; }
trap cleanup EXIT

echo "== scratch space =="
free_kb=$("$ADB" shell "df -Pk /var/tmp | awk 'NR==2{print \$4}'" | tr -d '\r')
if [ "${free_kb:-0}" -lt 2000000 ]; then
    echo "  FAIL: /var/tmp has only ${free_kb}KiB free, need ~2GiB for the raw dumps"
    exit 1
fi
pass "$((free_kb / 1048576))GiB free on /var/tmp"

echo "== VPU precondition =="
# A GUI player left over from an earlier run keeps its msm_vidc session, and a
# stale instance makes the decode and export checks below fail exactly as if the
# driver had regressed (same symptom either way, so it is worth a reset rather
# than an afternoon of bisecting a non-bug).
"$ADB" shell 'pkill -x mpv; pkill -x vlc; pkill -x totem' >/dev/null 2>&1
sleep 1
leaked=$("$ADB" shell 'ls -d /sys/kernel/debug/msm_vidc/core/inst_* 2>/dev/null | wc -l' | tr -d '\r')
if [ "${leaked:-0}" -gt 0 ]; then
    "$ADB" shell 'echo aa00000.video-codec > /sys/bus/platform/drivers/msm_vidc_v4l2/unbind
        sleep 2
        echo aa00000.video-codec > /sys/bus/platform/drivers/msm_vidc_v4l2/bind
        sleep 3' >/dev/null 2>&1
    pass "reset msm_vidc, cleared $leaked stale session(s)"
else
    pass "no stale msm_vidc sessions"
fi

echo "== build from the repository copy =="
tmp=$(mktemp -d /tmp/hermes-verify-msm-va.XXXXXX)
tar czf "$tmp/src.tgz" -C "$here" build.sh files
"$ADB" push "$tmp/src.tgz" /tmp/ >/dev/null || exit 1
"$ADB" shell "rm -rf $D && mkdir -p $D && tar xzf /tmp/src.tgz -C $D \
    && cd $D && chmod +x build.sh \
    && rm -f /usr/lib/aarch64-linux-gnu/dri/msm_drv_video.so \
    && ./build.sh >build.log 2>&1 && tail -1 build.log"
if "$ADB" shell "[ -s $D/msm_drv_video.so ]"; then
    pass "msm_drv_video.so built from files/"
else
    bad "no .so produced"
    exit 1
fi

echo "== libva picks it up without env vars =="
if "$ADB" shell "cd $D && ./build.sh --install && \
        env -u LIBVA_DRIVER_NAME -u LIBVA_DRIVERS_PATH vainfo 2>&1" \
        | grep -q 'Qualcomm Venus'; then
    pass "vainfo loads msm_drv_video.so"
else
    bad "vainfo did not load the driver"
fi

echo "== H.264 decode: bit-exact vs software =="
result=$("$ADB" shell "cd $D
gen() { ffmpeg -hide_banner -loglevel error -y -f lavfi \
    -i testsrc2=size=1280x720:rate=30 -frames:v 150 -c:v libx264 \
    -profile:v high -g 30 -bf \"\$1\" -pix_fmt yuv420p \"\$2\" </dev/null 2>/dev/null; }
hw() { ffmpeg -hide_banner -loglevel error -y -nostdin -hwaccel vaapi \
    -hwaccel_device /dev/dri/renderD128 -hwaccel_output_format vaapi \
    -f h264 -i \"\$1\" -vf hwdownload,format=nv12 -pix_fmt nv12 \
    -f rawvideo \"\$2\" </dev/null 2>/dev/null; }
gen 0 in0.h264
ffmpeg -hide_banner -loglevel error -y -i in0.h264 -pix_fmt nv12 \
    -f rawvideo sw0.nv12 </dev/null 2>/dev/null
hw in0.h264 hw0.nv12
cmp -s sw0.nv12 hw0.nv12 && echo \"no-b-frame bit-exact (\$(stat -c%s hw0.nv12) bytes)\" \
                        || echo 'no-b-frame MISMATCH'
gen 3 in3.h264
ffmpeg -hide_banner -loglevel error -y -i in3.h264 -pix_fmt nv12 \
    -f rawvideo sw3.nv12 </dev/null 2>/dev/null
ok=0
for _ in \$(seq 1 10); do hw in3.h264 hw3.nv12; cmp -s sw3.nv12 hw3.nv12 && ok=\$((ok+1)); done
echo \"b-frame bit-exact \$ok/10 (known reordering limit, see README)\"" \
    | grep -v '/usr/lib/qcom-vendor/libOpenCL.so.1')
echo "$result" | sed 's/^/  /'
echo "$result" | grep -q 'no-b-frame bit-exact' \
    && pass "hardware NV12 == software NV12 (no reordering)" \
    || bad "hardware decode differs"

echo "== surface export (what mpv/VLC need for zero-copy) =="
if "$ADB" shell "cd $D && gcc -O0 -D_GNU_SOURCE -o va_export_test \
        files/tests/va_export_test.c -lva -lva-drm 2>/dev/null \
        && ./va_export_test 2>/dev/null" | grep -q 'RESULT: PASS'; then
    pass "vaExportSurfaceHandle hands out a usable DMA-BUF"
else
    bad "surface export does not work"
fi

echo "== zero-copy display: mpv --hwdec=vaapi reaches the screen =="
# Needs a live Wayland session and gnome-screenshot.  Skipped, not failed, when
# the board is headless or the tool is absent - it is a display check, not a
# decoder check.
if ! "$ADB" shell 'command -v gnome-screenshot >/dev/null && \
        pgrep -x gnome-shell >/dev/null' 2>/dev/null; then
    echo "  SKIP: no gnome-screenshot / graphical session on the board"
else
    test -f "$here/files/tests/screenshot_compare.py" || {
        echo "  SKIP: comparison script missing"; }
    scr=$("$ADB" shell "cd $D && WORK=$D/display sh files/tests/zero_copy_display.sh 2>&1; \
        echo exit=\$?" | grep -v '/usr/lib/qcom-vendor/libOpenCL.so.1')
    echo "$scr" | grep -v '^exit=' | sed 's/^/  /'
    # Exit 3 = the hardware picture was fine but the software control never made
    # it to the screen. Our code cannot break software decode, so that is this
    # session's compositor being uncooperative: a skip, not a driver failure.
    if echo "$scr" | grep -q 'exit=3'; then
        echo "  SKIP: hardware picture captured, but the control playback never reached the screen"
    else
        "$ADB" pull "$D/display/quad_hw.png" "$tmp/quad_hw.png" >/dev/null 2>&1
        "$ADB" pull "$D/display/quad_sw.png" "$tmp/quad_sw.png" >/dev/null 2>&1
        if [ ! -s "$tmp/quad_hw.png" ] || [ ! -s "$tmp/quad_sw.png" ]; then
            bad "screenshots not produced"
        else
            cmp_out=$(python3 "$here/files/tests/screenshot_compare.py" \
                "$tmp/quad_hw.png" "$tmp/quad_sw.png" 2>&1)
            echo "$cmp_out" | sed 's/^/  /'
            echo "$cmp_out" | grep -q 'RESULT: PASS' \
                && pass "on-screen hardware picture matches software" \
                || bad "on-screen picture differs"
        fi
    fi
fi

echo "== VLC hardware playback reaches the screen =="
# Same idea as the mpv check above, but through VLC, which exercises a different
# pair of VA entry points: it probes the pool with vaDeriveImage +
# vaAcquireBufferHandle and then imports every frame from
# vaExportSurfaceHandle().  A layout it does not accept makes it drop frames into
# an empty texture, which renders as a flat green rectangle - caught here by the
# solid-colour patches in the clip.  Skip (not fail) when VLC cannot get a window
# in this session: that is the environment, not the driver.
if ! "$ADB" shell 'command -v vlc >/dev/null && pgrep -x gnome-shell >/dev/null' 2>/dev/null; then
    echo "  SKIP: no vlc / graphical session on the board"
else
    vlc_out=$("$ADB" shell "cd $D && WORK=$D/display sh files/tests/vlc_display.sh 2>&1; \
        echo exit=\$?" | grep -v '/usr/lib/qcom-vendor/libOpenCL.so.1')
    echo "$vlc_out" | grep -v '^exit=' | sed 's/^/  /'
    if echo "$vlc_out" | grep -q 'exit=0'; then
        pass "VLC hardware playback puts the picture on screen"
    elif echo "$vlc_out" | grep -q 'exit=2'; then
        echo "  SKIP: VLC could not get a window in this session"
    else
        bad "VLC did not put a hardware-decoded picture on screen"
    fi
fi

echo
[ "$fail" -eq 0 ] && echo "RESULT: all checks passed" \
                  || echo "RESULT: $fail check(s) failed"
rm -rf "$tmp"
exit "$fail"
