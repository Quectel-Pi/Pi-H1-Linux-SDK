#!/bin/sh

# 工厂模式下 pipewire 以 pi 身份运行，但工厂镜像的 pi 只有自身组，读不了
# /dev/input/event* 上的耳机插拔开关(SW_HEADPHONE_INSERT/SW_MICROPHONE_INSERT)：
# pw-pal-plugin 拿不到插拔状态，PAL 就不会 open PAL_DEVICE_IN_WIRED_HEADSET，
# pw-record 录出来长度正常但样本全 0 —— AT+HEADSET_START 看起来成功其实没测到。
#
# 这里不给 pi 加组，而是直接放开设备节点权限：产测后续可能不再使用 pi 用户，
# 按节点授权对任何用户都成立，也不用重启会话才能生效(组只在登录时取一次)。
grant_node_perms() {
    local granted=0 name dev

    # 耳机/HDMI 插孔：从 sysfs 读设备名(所有用户可读)筛出 Jack 设备，
    # 只放开这几个 event 节点，不动触摸屏等其他输入设备。
    for name in /sys/class/input/event*/device/name; do
        [ -r "$name" ] || continue
        grep -q "Jack" "$name" 2>/dev/null || continue
        dev="/dev/input/$(basename "$(dirname "$(dirname "$name")")")"
        [ -e "$dev" ] || continue
        if [ "$(stat -c %a "$dev" 2>/dev/null)" != "666" ]; then
            chmod 666 "$dev" 2>/dev/null || continue
            echo "[atci] $dev -> 666 ($(cat "$name" 2>/dev/null))"
        fi
        granted=1
    done

    # 音频/视频节点：等价于原来把用户加进 audio/video 组(660 root:audio|video)。
    for dev in /dev/snd/* /dev/video*; do
        [ -e "$dev" ] || continue
        chmod 666 "$dev" 2>/dev/null || true
    done

    # pi 用户需要访问 /dev/dma_heap/qcom,audio-ml 才能启动 pipewire(AGM init)。
    # 默认权限 660 (system:audio)，故统一放开 dma_heap 设备权限。
    for dev in /dev/dma_heap/*; do
        [ -e "$dev" ] || continue
        chmod 666 "$dev" 2>/dev/null || true
    done

    return $((1 - granted))
}

# 声卡可能比 atci 晚一点注册，Jack 节点没出现就重试(最多 10s)，
# 否则后面的 AT+HEADSET_START 会因为拿不到插拔状态而误判。
i=0
while [ $i -lt 20 ]; do
    if grant_node_perms; then
        break
    fi
    i=$((i + 1))
    [ $i -eq 20 ] && echo "[atci] WARN: no Jack input device found, headset detection may fail"
    sleep 0.5
done

while [ ! -e /dev/ttyGS0 ]; do
    echo "Waiting for /dev/ttyGS0..."
    sleep 1
done

# 等待期间 udev 可能又重设过权限，起 atcid 前再确认一次。
grant_node_perms || true

exec /usr/sbin/atcid
