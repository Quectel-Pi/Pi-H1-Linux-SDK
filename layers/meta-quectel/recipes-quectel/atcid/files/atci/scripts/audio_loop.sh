#!/bin/sh
# Headset record-then-playback via PipeWire (pi user session), invoked by AT+HEADSET_START.
# Records from the headset mic, plays it back on the headset.
# Does NOT touch DSP/tinymix/gdm.

log() { echo "[HEADSET] $*"; }

# ---- ensure audio dma_heap is accessible (atci_init.sh also does this) ----
chmod 666 /dev/dma_heap/* 2>/dev/null || true

# ---- find the user session runtime dir (prefer pi/uid 1001, else any active) ----
RT=""
for d in /run/user/1001 /run/user/*; do
    [ -d "$d" ] || continue
    if [ -S "$d/pipewire-0" ] || [ -S "$d/pulse/native" ]; then RT="$d"; break; fi
done
log "session runtime dir: ${RT:-none}"

if [ -z "$RT" ]; then
    log "no pipewire session found, skip"
    exit 0
fi

UID_NUM=$(basename "$RT")
REC_WAV=/tmp/headset_rec.wav
REC_SECS=${HEADSET_REC_SECS:-3}

# run a command as the session user (or directly if root session)
run_as_user() {
    if [ "$UID_NUM" = "1001" ]; then
        su - pi -c "XDG_RUNTIME_DIR=$RT $*"
    else
        XDG_RUNTIME_DIR="$RT" "$@"
    fi
}

# ---- record from headset mic ----
rm -f "$REC_WAV"
log "pw-record start (${REC_SECS}s from headset mic)"
run_as_user "timeout ${REC_SECS} pw-record --target pal_source_headset_mic --channels 2 --rate 48000 $REC_WAV"
log "pw-record done rc=$?"

if [ ! -s "$REC_WAV" ]; then
    log "no audio captured, skip playback"
    exit 0
fi
SZ=$(stat -c%s "$REC_WAV" 2>/dev/null || echo 0)
if [ "$SZ" -le 44 ]; then
    log "captured only empty wav header (${SZ}B), skip playback"
    exit 0
fi
log "captured ${SZ}B"

# ---- play back on headset ----
log "pw-play start (headset sink)"
run_as_user "pw-play --target pal_sink_headset $REC_WAV"
log "pw-play done rc=$?"

rm -f "$REC_WAV"
log "done"
