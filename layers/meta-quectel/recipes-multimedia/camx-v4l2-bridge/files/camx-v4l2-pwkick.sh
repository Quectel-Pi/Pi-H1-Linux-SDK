#!/bin/sh
#
# camx-v4l2-pwkick — kick PipeWire/wireplumber to reprobe /dev/video10
#
# Why: PipeWire's spa-v4l2 caches device_caps at first probe. On cold boot it
# races the camx bridge and caches the initial M2M value (capture+output),
# which blocks format negotiation (gnome-snapshot / pipewiresrc can't connect).
# After the bridge has written a black frame, device_caps becomes pure capture.
# At that point restarting wireplumber forces spa-v4l2 to reprobe and cache the
# correct caps, so the Video/Source node's format becomes negotiable.
#
# Runs as root (from a system unit ExecStartPost); finds the active graphical
# session user and restarts their wireplumber user unit.
#
set -u

DEV="${CAMX_V4L2_DEVICE:-/dev/video10}"
LOG_TAG="camx-v4l2-pwkick"

log() { logger -t "$LOG_TAG" "$*"; [ -n "${DEBUG:-}" ] && echo "$LOG_TAG: $*"; }

# 1) Wait for the bridge to actually flip caps to pure capture.
#    Idle v4l2loopback reports capture+output (0x05200003); only after the
#    bridge opens O_WRONLY and writes a frame does it become capture-only
#    (output bit 0x2 clears). We poll device_caps and require that the
#    V4L2_CAP_VIDEO_OUTPUT bit (0x2) be clear AND capture (0x1) be set.
#    Wait generously: the bridge does ExecStartPre sleep 8 + cam-server
#    connect (~1-2s) + probe + write_black_frame, which can take >10s.
wait_caps_ok() {
    local tries=0 max=200  # 200 * 0.3s = 60s
    while [ "$tries" -lt "$max" ]; do
        caps=$(v4l2-ctl -d "$DEV" --all 2>/dev/null \
               | sed -n 's/.*Device Caps.*: 0x\([0-9a-fA-F]*\).*/\1/p' \
               | head -1 | tr -d ' \r')
        [ -z "$caps" ] && caps=0
        # shell arithmetic: accept hex prefix 0x
        h="0x$caps"
        cap_bit=$(( h & 0x1 ))
        out_bit=$(( h & 0x2 ))
        if [ "$cap_bit" -ne 0 ] && [ "$out_bit" -eq 0 ]; then
            log "caps=0x$caps (pure capture, output bit clear) OK"
            return 0
        fi
        sleep 0.3
        tries=$((tries + 1))
    done
    log "timeout waiting caps (last=0x$caps); skipping kick"
    return 1
}

# 2) Find the active graphical session user (who owns seat0 active session).
find_session_user() {
    # Prefer loginctl active seat0 session
    if command -v loginctl >/dev/null 2>&1; then
        for s in $(loginctl list-sessions --no-legend 2>/dev/null | awk '{print $1}'); do
            seat=$(loginctl show-session "$s" -p Seat --value 2>/dev/null)
            active=$(loginctl show-session "$s" -p Active --value 2>/dev/null)
            class=$(loginctl show-session "$s" -p Class --value 2>/dev/null)
            if [ "$seat" = "seat0" ] && [ "$active" = "yes" ] \
               && [ "$class" = "user" ]; then
                loginctl show-session "$s" -p Name --value 2>/dev/null
                return 0
            fi
        done
    fi
    # Fallback: newest /run/user/X owned by a real user
    for d in /run/user/*; do
        [ -d "$d" ] || continue
        [ "${d##*/}" -ge 1000 ] 2>/dev/null || continue
        stat -c '%U' "$d" 2>/dev/null
        return 0
    done
    return 1
}

# 3) Restart the user's wireplumber (not pipewire — keep audio alive).
kick_wireplumber() {
    local user="$1"
    local uid
    uid=$(id -u "$user" 2>/dev/null) || { log "no uid for $user"; return 1; }
    local xdg="/run/user/$uid"
    local bus="unix:path=$xdg/bus"
    [ -S "$xdg/bus" ] || { log "no user bus at $xdg/bus"; return 1; }

    # Only kick if wireplumber unit exists for that user
    if ! su - "$user" -c "XDG_RUNTIME_DIR=$xdg DBUS_SESSION_BUS_ADDRESS=$bus \
                          systemctl --user cat wireplumber >/dev/null 2>&1"; then
        log "wireplumber unit not found for $user; nothing to kick"
        return 0
    fi

    # Restart with retry; wireplumber coming back up re-enumerates v4l2 devices
    local r=0 maxr=3
    while [ "$r" -lt "$maxr" ]; do
        if su - "$user" -c "XDG_RUNTIME_DIR=$xdg DBUS_SESSION_BUS_ADDRESS=$bus \
                            systemctl --user restart wireplumber 2>&1"; then
            log "wireplumber restart OK (attempt $((r+1)))"
            # give spa-v4l2 a moment to reprobe
            sleep 4
            return 0
        fi
        r=$((r+1)); sleep 1
    done
    log "wireplumber restart failed after $maxr attempts"
    return 1
}

wait_caps_ok || exit 0
user=$(find_session_user)
[ -n "$user" ] || { log "no session user found; nothing to do"; exit 0; }
log "session user=$user"
kick_wireplumber "$user" || exit 0
exit 0
