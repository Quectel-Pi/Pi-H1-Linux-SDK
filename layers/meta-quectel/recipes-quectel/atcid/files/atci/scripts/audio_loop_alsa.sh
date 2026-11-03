#!/bin/sh
# Headset record-then-playback via PipeWire (Yocto/weston image, no logged-in
# user session), invoked by AT+HEADSET_START when is_gnome_session_running()
# is false.
# 耳机录音 -> 静音判定 -> 耳机回放，与 Debian 版 audio_loop.sh 语义一致。
#
# 背景：PAL/AGM 是这块板唯一能出声的通路（raw ALSA 假通、agmplay 裸 CLI 被
# ADSP 拒绝）。Debian 镜像靠登录会话里的 pipewire + pw-pal-plugin 出声；
# Yocto weston 镜像里 pipewire.service 由 systemd 以系统级方式拉起（以
# pipewire 用户运行，socket 在 /run/pipewire），pw-pal-plugin.conf 自动加载
# 6 个 pal 节点。优先复用这个实例；只有它不存在时才回退到隔离 PW_RUNTIME_DIR
# 的独立实例（再起第二个实例会抢不到音频路径，pal_stream_open 返回 -11）。
#
# 退出码: 0 成功, 3 耳机没插, 4 没录到文件, 5 录到纯静音
# (AT+HEADSET_START 只回 OK，结果看 journal 里的 [HEADSET-PW] PASS/FAIL 行)

log() { echo "[HEADSET-PW] $*"; }

REC_WAV=/tmp/headset_rec.wav
REC_SECS=${HEADSET_REC_SECS:-3}

# ---- 1. 耳机插了吗 -----------------------------------------------------------
# 耳机孔是 SND_JACK_HEADSET(SW_HEADPHONE_INSERT | SW_MICROPHONE_INSERT)。
# 不插耳机时录音一样返回 0、写出完整长度的 wav，只是内容全 0，
# 所以必须先读 jack 状态，否则产测会把"没插耳机"判成通过。
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

# ---- GPIO: 耳机供电/美标选择 (照抄原版 init_gpio, rgs 缺失时仅告警) ----
# 模拟部件的供电和通路选择不走 PAL，必须产测前设好
if ! command -v rgs >/dev/null 2>&1; then
    log "WARN: rgs not found, skip headset power gpio"
else
    pgrep -x rgpiod >/dev/null 2>&1 || { rgpiod & sleep 1; }
    rgs c 999 go 4
    rgs c 999 gso 0 8
    rgs c 999 gw 0 8 1   # 美标耳机
    rgs c 999 gso 0 68
    rgs c 999 gw 0 68 1  # 启用耳机供电
fi

chmod 666 /dev/dma_heap/* 2>/dev/null || true

# ---- 2. 选 pipewire 实例 -----------------------------------------------------
# 镜像的 pipewire.service 是 systemd 系统服务，以 pipewire 用户运行，socket
# 在 /run/pipewire，它才真正持有 PAL/ADSP。再起一个独立实例会抢不到音频
# 路径：pal_stream_open 返回 -11(EAGAIN "no audio path yet")，录音正常出文件
# 但样本全零。所以系统实例存在就直接复用（产测完只还原默认设备，不杀进程）；
# 只有它不存在时才回退到隔离 PW_RUNTIME_DIR 的独立实例。
restore_defaults() {
    [ -n "$XDG_RUNTIME_DIR" ] || return 0
    pw-metadata -n default -d 0 default.configured.audio.source >/dev/null 2>&1
    pw-metadata -n default -d 0 default.configured.audio.sink >/dev/null 2>&1
}

CLEANUP_PIDS=""
cleanup() {
    restore_defaults
    [ -n "$CLEANUP_PIDS" ] && kill $CLEANUP_PIDS 2>/dev/null
    rm -f "$REC_WAV"
}
trap cleanup EXIT INT TERM

SYS_RT=/run/pipewire
if [ -S "$SYS_RT/pipewire-0" ]; then
    XDG_RUNTIME_DIR=$SYS_RT
    export XDG_RUNTIME_DIR
    log "using system pipewire instance ($SYS_RT)"
else
    XDG_RUNTIME_DIR=/tmp/pw-headset-test
    export XDG_RUNTIME_DIR
    OLD_PW=$(pgrep -f "$XDG_RUNTIME_DIR" 2>/dev/null)
    [ -n "$OLD_PW" ] && kill $OLD_PW 2>/dev/null
    rm -rf "$XDG_RUNTIME_DIR"
    mkdir -p "$XDG_RUNTIME_DIR"
    chmod 700 "$XDG_RUNTIME_DIR"

    /usr/bin/pipewire >/tmp/pw-headset.log 2>&1 &
    PW_PID=$!
    CLEANUP_PIDS="$CLEANUP_PIDS $PW_PID"
    sleep 3

    /usr/bin/wireplumber >/tmp/wp-headset.log 2>&1 &
    WP_PID=$!
    CLEANUP_PIDS="$CLEANUP_PIDS $WP_PID"
    sleep 5

    kill -0 $PW_PID 2>/dev/null || { log "FAIL: pipewire died (see /tmp/pw-headset.log)"; exit 4; }
    kill -0 $WP_PID 2>/dev/null || log "WARN: wireplumber died, links may not form"
fi

# pal 节点就位检查(ADSP 图初始化慢时多等几轮)
NODE_OK=""
for i in 1 2 3 4 5; do
    if timeout 5 /usr/bin/pw-cli ls Node 2>/dev/null | grep -q '"pal_sink_headset"'; then
        NODE_OK=1
        break
    fi
    sleep 2
done
[ -n "$NODE_OK" ] || { log "FAIL: pal nodes never appeared (see /tmp/pw-headset.log)"; exit 4; }
log "pipewire ready, pal nodes up"

# ---- 2.5 把默认 sink/source 钉到耳机节点 -------------------------------------
# pw-pal 插件的 pw_pal_is_selected() 只给"被选为默认"的节点开 pal_stream:
# wireplumber 自动选的默认 source 可能落在 pal_source_speaker_mic 上，此时对
# pal_source_headset_mic 录音节点会正常 STREAMING，但 TX 流被静默关掉、
# pal_stream_open 永远不执行，录出全零 wav（Yocto 板实测复现，peak=0）。
# default.configured.audio.* 优先级高于 default.audio.*，且节点名固定不受
# wireplumber 策略影响，产测场景直接钉死即可。
pw-metadata -n default 0 default.configured.audio.source \
    '{"name":"pal_source_headset_mic"}' >/dev/null 2>&1
pw-metadata -n default 0 default.configured.audio.sink \
    '{"name":"pal_sink_headset"}' >/dev/null 2>&1
sleep 1
log "default source/sink pinned to headset nodes"

# ---- 3. 录 headset mic -------------------------------------------------------
# pw-record 没有时长参数，用 timeout 截断(${REC_SECS}s)，成败只看文件大小和峰值
rm -f "$REC_WAV"
log "pw-record start (${REC_SECS}s from headset mic)"
timeout "$REC_SECS" /usr/bin/pw-record --target pal_source_headset_mic \
    --rate 48000 --channels 2 --format s16 "$REC_WAV" 2>/dev/null
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

# ---- 4. 录到的到底是不是声音 -------------------------------------------------
# 节点选错/mic 通路没通时，wav 长度正常但样本全 0。只看文件大小会误判通过。
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
    log "FAIL: recorded silence (peak=0), headset mic path was not opened"
    exit 5
fi

# ---- 5. 耳机回放 -------------------------------------------------------------
log "pw-play start (headset)"
timeout 15 /usr/bin/pw-play --target pal_sink_headset "$REC_WAV" 2>/dev/null
log "pw-play done rc=$?"

log "PASS: peak=${PEAK} rms=${RMS:-?}"
exit 0
