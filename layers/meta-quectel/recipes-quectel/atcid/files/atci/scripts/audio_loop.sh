#!/bin/sh
# Headset record-then-playback via PipeWire (pi user session), invoked by AT+HEADSET_START.
# Records from the headset mic, plays it back on the headset.
# Does NOT touch DSP/tinymix/gdm.
#
# 退出码: 0 成功, 2 无 pipewire 会话, 3 耳机没插, 4 没录到文件, 5 录到纯静音
# (AT+HEADSET_START 只回 OK，结果看 journal 里的 [HEADSET] PASS/FAIL 行)

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
    log "FAIL: no pipewire session found"
    exit 2
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

# ---- 1. 耳机插了吗 -----------------------------------------------------------
# 耳机孔是 SND_JACK_HEADSET(SW_HEADPHONE_INSERT | SW_MICROPHONE_INSERT)。
# 不插耳机时 pipewire 一样返回 0、一样写出完整长度的 wav，只是内容全 0，
# 所以必须先读 jack 状态，否则产测会把"没插耳机"判成通过。
# 本脚本以 root 运行(由 atcid 拉起)，能读 /dev/input；pipewire 以 pi 身份运行，
# 它的 /dev/input 权限由 atci_init.sh 补 input 组解决。
JACK_STATE=$(python3 - <<'PY' 2>/dev/null
import fcntl, glob, os, sys

# ioctl 请求号必须带长度: 只写 0x80084506 等于请求 8 字节, EVIOCGNAME 会把设备名
# 截断成 8 个字符("qcm6490-"), strstr 永远匹配不上。
# _IOC(_IOC_READ, 'E', 0x06, 256) / _IOC(_IOC_READ, 'E', 0x1b, sizeof(unsigned long))
def _ioc_read(nr, size, typ=ord('E')):
    return (2 << 30) | (size << 16) | (typ << 8) | nr

EVIOCGNAME = _ioc_read(0x06, 256)
EVIOCGSW = _ioc_read(0x1B, 8)
SW_HEADPHONE_INSERT, SW_MICROPHONE_INSERT = 2, 4  # linux/input-event-codes.h
JACK_BITS = (1 << SW_HEADPHONE_INSERT) | (1 << SW_MICROPHONE_INSERT)

for dev in sorted(glob.glob('/dev/input/event*')):
    try:
        fd = os.open(dev, os.O_RDONLY | os.O_NONBLOCK)
    except OSError:
        continue
    try:
        name = bytearray(256)
        fcntl.ioctl(fd, EVIOCGNAME, name, True)
        if b'Headset Jack' not in bytes(name):
            continue
        buf = bytearray(8)
        fcntl.ioctl(fd, EVIOCGSW, buf, True)
        sw = int.from_bytes(buf, 'little')
        print('plugged' if sw & JACK_BITS else 'unplugged')
        sys.exit(0)
    finally:
        os.close(fd)
print('unknown')
PY
)
log "jack state: ${JACK_STATE:-unknown}"

if [ "$JACK_STATE" = "unplugged" ]; then
    log "FAIL: headset not plugged in"
    exit 3
fi
if [ "$JACK_STATE" != "plugged" ]; then
    # 读不到 jack 事件时仍继续，由下面的静音判定兜底，但把线索打出来
    log "WARN: headset jack state unknown"
fi

# ---- 2. 录 headset mic -------------------------------------------------------
rm -f "$REC_WAV"
log "pw-record start (${REC_SECS}s from headset mic)"
run_as_user "timeout ${REC_SECS} pw-record --target pal_source_headset_mic --channels 2 --rate 48000 $REC_WAV"
# 录音没有自然的结束条件，timeout 杀掉 pw-record 属正常路径(rc=124)
log "pw-record done rc=$?"

if [ ! -s "$REC_WAV" ]; then
    log "FAIL: no audio captured"
    exit 4
fi

SZ=$(stat -c%s "$REC_WAV" 2>/dev/null || echo 0)
if [ "$SZ" -le 44 ]; then
    log "FAIL: captured only an empty wav header (${SZ}B)"
    exit 5
fi

# ---- 3. 录到的到底是不是声音 -------------------------------------------------
# 设备选错/pipewire 没权限打不开 /dev/input 时，PAL 不会 open headset mic，
# wav 长度正常但样本全 0。只看文件大小会误判通过，所以算 peak/RMS。
STAT=$(python3 - "$REC_WAV" <<'PY' 2>/dev/null
import array, math, sys, wave
try:
    w = wave.open(sys.argv[1])
except Exception:
    print('0 0')
    raise SystemExit
a = array.array('h')
a.frombytes(w.readframes(w.getnframes()))
if not a:
    print('0 0')
else:
    print('%d %.1f' % (max(abs(x) for x in a),
                       math.sqrt(sum(x * x for x in a) / len(a))))
PY
)
PEAK=${STAT%% *}
RMS=${STAT##* }
case "$PEAK" in
    ''|*[!0-9]*) PEAK=0 ;;
esac
log "captured ${SZ}B peak=${PEAK} rms=${RMS:-?}"

if [ "$PEAK" -lt 1 ]; then
    rm -f "$REC_WAV"
    log "FAIL: recorded silence (peak=0), headset mic was not opened"
    exit 5
fi

# ---- 4. 耳机播放 -------------------------------------------------------------
log "pw-play start (headset sink)"
run_as_user "pw-play --target pal_sink_headset $REC_WAV"
log "pw-play done rc=$?"

rm -f "$REC_WAV"
log "PASS: peak=${PEAK} rms=${RMS:-?}"
