#!/bin/bash
# On-device half of the zero-copy display check (driven by ../verify.sh).
#
# Plays a 2x2 solid-colour clip through mpv twice - once with VA-API hardware
# decoding, once with software decoding - and screenshots the screen during
# each.  The two PNGs land next to this script's working directory for the host
# side to compare.
#
# Requires: a live graphical session, gnome-screenshot, mpv, ffmpeg.
set -u

WORK=${WORK:-/var/tmp/msm-va-display}
SESSION_USER=${SESSION_USER:-p}
WAYLAND_DISPLAY_NAME=${WAYLAND_DISPLAY_NAME:-wayland-0}
CLIP_SECONDS=8

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

as_session_user() {
    su "$SESSION_USER" -c "cd $WORK && XDG_RUNTIME_DIR=$runtime_dir \
        WAYLAND_DISPLAY=$WAYLAND_DISPLAY_NAME $1"
}

screenshot() {
    local out=$1
    local pid addr
    rm -f "$WORK/$out"
    pid=$(pgrep -u "$SESSION_USER" -x gnome-shell | head -1)
    [ -n "$pid" ] || { echo "  gnome-shell not running for $SESSION_USER"; return 1; }
    addr=$(tr '\0' '\n' < "/proc/$pid/environ" 2>/dev/null \
        | sed -n 's/^DBUS_SESSION_BUS_ADDRESS=//p')
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

# Is the clip actually on screen right now?
#
# mpv logging its video output does not mean the compositor has put it up yet,
# so sampling the screenshot is the only trustworthy answer.  No image library
# is needed on the board: crop a patch inside a known quadrant and average it
# to one pixel with ffmpeg.  The clip is red over blue on the left half, and
# the patches are chosen to fall inside those quadrants both when the video is
# played full screen and when it is windowed, so this works either way.
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

# Wait until the player is actually putting frames on the screen before
# screenshotting.  A fixed sleep races window mapping: sampling too early
# captures the desktop, which then looks like a decode mismatch rather than a
# timing artefact.  mpv logs a "VO: [gpu] <size> <format>" line once its video
# output is up, so wait for that.
wait_for_video_output() {
    local log=$1 player=$2 attempt

    for attempt in $(seq 1 30); do
        kill -0 "$player" 2>/dev/null || return 1
        grep -q '^VO: \[' "$log" 2>/dev/null && return 0
        sleep 0.5
    done
    return 1
}

# mpv must be completely gone before the next phase: a lingering instance keeps
# the Wayland surface (and a VPU session) and the next player may never get one.
# Match on a unique --title instead of "any mpv": an unrelated player someone
# left running on the board would otherwise make this wait forever.
wait_for_player_exit() {
    local tag=$1 attempt

    pkill -f "msm-va-display-$tag" 2>/dev/null
    for attempt in $(seq 1 20); do
        pgrep -f "msm-va-display-$tag" >/dev/null 2>&1 || return 0
        sleep 0.5
    done
    echo "  warning: player for $tag still running after 10s"
    return 1
}

# $1 = hwdec mode, $2 = output tag
play_and_capture() {
    local hwdec=$1 tag=$2
    local log=$WORK/mpv_$tag.log
    local launch player attempt

    # Relaunching is the retry that matters.  Re-screenshotting only helps when
    # the window is up but the compositor has not put the first frame on screen
    # yet; when the surface never got mapped at all - which happens when the
    # previous fullscreen player has just been torn down - every screenshot is
    # just the desktop, no matter how many times it is repeated.  So retry the
    # player, not only the capture.
    for launch in 1 2 3; do
        : > "$log"
        # --fs matters: a windowed player can end up behind another window, and
        # then the screenshot is the desktop no matter how long we wait.
        as_session_user "mpv --no-config --vo=gpu --gpu-context=wayland --fs \
            --no-border --hwdec=$hwdec --loop-file=inf --no-audio \
            --msg-level=all=info --title=msm-va-display-$tag quad.mp4" \
            >"$log" 2>&1 &
        player=$!

        if wait_for_video_output "$log" "$player"; then
            echo "  $tag: video output up"
        else
            echo "  $tag: video output never came up"
            grep -iE "error|fail" "$log" | head -3
        fi
        sleep 2

        # Keep re-capturing until the frame really shows the clip.  Without this
        # the check reports a capture race as if the decoded picture were wrong.
        for attempt in 1 2 3 4 5; do
            if screenshot "quad_$tag.png" && clip_is_on_screen "$WORK/quad_$tag.png"; then
                break
            fi
            echo "  $tag: capture $attempt did not catch the clip, retrying"
            sleep 1
        done
        clip_is_on_screen "$WORK/quad_$tag.png" && on_screen=1 || on_screen=0

        kill "$player" 2>/dev/null
        wait_for_player_exit "$tag"
        sleep 1

        [ "$on_screen" -eq 1 ] && return 0
        echo "  $tag: player never put the clip on screen, relaunching ($launch/3)"
    done

    echo "  $tag: never captured the clip on screen"
    return 1
}

hw_ok=0
sw_ok=0
echo "hardware decode:"
play_and_capture vaapi hw && hw_ok=1
echo "software decode:"
play_and_capture no sw && sw_ok=1

for f in quad_hw.png quad_sw.png; do
    [ -s "$WORK/$f" ] || { echo "missing $f"; exit 1; }
done
echo "screenshots ready in $WORK"

# Exit status tells verify.sh which kind of trouble this was:
#   1 = the hardware picture is missing or wrong - a real failure, and what a
#       broken driver looks like (a flat green window has no test pattern);
#   3 = the hardware picture is fine but the software control never reached the
#       screen.  Software decode cannot be broken by our VA-API code, so this is
#       an environment flake and verify.sh reports it as a skip, not a failure.
[ "$hw_ok" -eq 1 ] || exit 1
[ "$sw_ok" -eq 1 ] || exit 3
exit 0
