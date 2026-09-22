#!/bin/bash
# On-device half of the VLC zero-copy display check (driven by ../verify.sh).
#
# Plays the same 2x2 solid-colour clip as zero_copy_display.sh through VLC with
# VA-API hardware decoding and screenshots the screen.  Red-over-blue patches
# have to be on screen: VLC's GL interop silently drops every frame into an
# empty texture when the driver answers vaExportSurfaceHandle() with a layout it
# does not accept, and an empty texture renders as a flat green rectangle -
# which fails the patch checks here instead of looking like a pass.
#
# -I dummy on purpose: the checks are about the decoder and the video output,
# and this way VLC does not need a Qt platform plugin to run.
#
# Requires: a live graphical session, gnome-screenshot, vlc, ffmpeg.
set -u

WORK=${WORK:-/var/tmp/msm-va-display}
SESSION_USER=${SESSION_USER:-p}
WAYLAND_DISPLAY_NAME=${WAYLAND_DISPLAY_NAME:-wayland-0}
CLIP_SECONDS=8
WAIT_SECONDS=${WAIT_SECONDS:-20}

uid_of_session_user=$(id -u "$SESSION_USER" 2>/dev/null) || {
    echo "no such user: $SESSION_USER"; exit 1; }
runtime_dir=/run/user/$uid_of_session_user

mkdir -p "$WORK"
chmod 777 "$WORK"
cd "$WORK" || exit 1

if [ ! -s quad.mp4 ]; then
    ffmpeg -hide_banner -loglevel error -y \
        -f lavfi -i "color=c=red:s=640x360:d=$CLIP_SECONDS:r=30" \
        -f lavfi -i "color=c=lime:s=640x360:d=$CLIP_SECONDS:r=30" \
        -f lavfi -i "color=c=blue:s=640x360:d=$CLIP_SECONDS:r=30" \
        -f lavfi -i "color=c=white:s=640x360:d=$CLIP_SECONDS:r=30" \
        -filter_complex "[0:v][1:v]hstack[t];[2:v][3:v]hstack[b];[t][b]vstack" \
        -c:v libx264 -profile:v high -bf 0 -g 30 -pix_fmt yuv420p \
        quad.mp4 </dev/null 2>/dev/null || {
        echo "failed to generate quad.mp4"; exit 1; }
fi
chmod 644 quad.mp4

# VLC needs the session bus as well as the compositor socket when it is started
# from a script rather than from the desktop.
session_bus_address() {
    local pid

    pid=$(pgrep -u "$SESSION_USER" -x gnome-shell | head -1)
    [ -n "$pid" ] || return 1
    tr '\0' '\n' < "/proc/$pid/environ" 2>/dev/null \
        | sed -n 's/^DBUS_SESSION_BUS_ADDRESS=//p' | head -1
}

screenshot() {
    local out=$1 addr

    rm -f "$WORK/$out"
    addr=$(session_bus_address) || { echo "  gnome-shell not running"; return 1; }
    su "$SESSION_USER" -c "DBUS_SESSION_BUS_ADDRESS='$addr' \
        XDG_RUNTIME_DIR=$runtime_dir gnome-screenshot -f $WORK/$out" \
        >/dev/null 2>&1
    if [ -s "$WORK/$out" ]; then
        echo "  $out captured ($(stat -c%s "$WORK/$out") bytes)"
        return 0
    fi
    echo "  $out not captured"
    return 1
}

# No image library is needed on the board: crop a patch and average it to one
# pixel with ffmpeg.  The clip is red over blue on the left half, and the
# patches fall inside those quadrants whether VLC is fullscreen or windowed.
sample_patch() {
    local png=$1 x=$2 y=$3

    ffmpeg -v error -i "$png" -vf "crop=48:48:$x:$y,scale=1:1" \
        -f rawvideo -pix_fmt rgb24 - 2>/dev/null | od -An -tu1 | tr -s ' '
}

patch_is_red() {
    set -- $(sample_patch "$1" "$2" "$3")
    [ $# -eq 3 ] && [ "$1" -ge 170 ] && [ "$2" -le 100 ] && [ "$3" -le 100 ]
}

patch_is_blue() {
    set -- $(sample_patch "$1" "$2" "$3")
    [ $# -eq 3 ] && [ "$3" -ge 170 ] && [ "$1" -le 100 ] && [ "$2" -le 100 ]
}

clip_is_on_screen() {
    patch_is_red "$1" 400 300 && patch_is_blue "$1" 400 700
}

log=$WORK/vlc.log

bus=$(session_bus_address) || { echo "no graphical session, cannot test"; exit 1; }

# A GNOME session keeps XWayland's cookie in the runtime directory rather than
# ~/.Xauthority, so a client that falls back to xcb has to be told where it is.
xauth=$(ls "$runtime_dir"/.mutter-Xwaylandauth.* 2>/dev/null | head -1)

# A leftover player keeps the session busy and the next VLC can fail to get a
# window, which would look like a driver regression.  Wait for it to be gone.
for attempt in $(seq 1 10); do
    pgrep -x vlc >/dev/null 2>&1 || break
    [ "$attempt" -eq 1 ] && pkill -x vlc 2>/dev/null
    sleep 0.5
done
pgrep -x vlc >/dev/null 2>&1 && { echo "a VLC instance is already running"; exit 2; }

# VLC needs an interface that can give its video output a window, and which Qt
# platform plugin provides one depends on the rootfs.  Try the session default
# first, then pin xcb (XWayland) which needs no extra packages.
# VLC_HW overrides the decoder's hardware backend, and VLC_HW="" runs VLC the way
# a plain menu entry would (no flag at all), which is how the packaged default is
# checked.
hw_arg=${VLC_HW---avcodec-hw=vaapi}

launch_once() {
    local platform=$1

    : > "$log"
    su "$SESSION_USER" -c "cd $WORK && XDG_RUNTIME_DIR=$runtime_dir \
        WAYLAND_DISPLAY=$WAYLAND_DISPLAY_NAME DISPLAY=${DISPLAY_NAME:-:0} \
        ${xauth:+XAUTHORITY=$xauth} \
        DBUS_SESSION_BUS_ADDRESS='$bus' ${platform:+QT_QPA_PLATFORM=$platform} \
        VENUS_VAAPI_LOG=1 vlc -I qt $hw_arg \
        --no-audio --fullscreen --loop quad.mp4" \
        >"$log" 2>&1 &
    player=$!

    # "capture surface=" is the driver handing a decoded frame to a client
    # surface, so it proves the decoder is running - not merely that VLC started.
    for _ in $(seq 1 $((WAIT_SECONDS * 2))); do
        kill -0 "$player" 2>/dev/null || return 1
        grep -q 'capture surface=' "$log" 2>/dev/null && return 0
        sleep 0.5
    done
    return 1
}

decoding=0
for platform in "" xcb; do
    if launch_once "$platform"; then
        decoding=1
        echo "  decoder running (QT_QPA_PLATFORM=${platform:-default})"
        break
    fi
    echo "  no video window with QT_QPA_PLATFORM=${platform:-default}, retrying"
    pkill -x vlc 2>/dev/null
    sleep 2
done

if [ "$decoding" -ne 1 ]; then
    echo "  VLC never got a window to put its video output in"
    grep -aiE "interface|vout display" "$log" | head -5
    pkill -x vlc 2>/dev/null
    exit 2   # environment problem, not a driver verdict
fi

captured=0
for attempt in 1 2 3 4 5; do
    if screenshot "quad_vlc.png" && clip_is_on_screen "$WORK/quad_vlc.png"; then
        captured=1
        break
    fi
    echo "  capture $attempt did not catch the clip, retrying"
    sleep 1
done

echo "  hardware decode: $(grep -ac 'venus-vaapi' "$log") driver calls logged"
echo "  zero-copy fds  : $(grep -ac 'export-surface' "$log") exports"
echo "  gl errors      : $(grep -ac 'gl error' "$log")"

pkill -x vlc 2>/dev/null
sleep 1

if [ "$captured" -ne 1 ]; then
    echo "  VLC never put the clip on screen"
    exit 1
fi
echo "  screenshot ready in $WORK/quad_vlc.png"
