#!/bin/sh
#
# /var/persist/fct_done_flag 不存在 = 还在产测流程里，Type-C 口锁成 client(device)，
# 这样只接 D+/D-/GND 三根线时上位机也能枚举到 gadget(05c6:902d) 和 AT 口 /dev/ttyGS0。
#
# 只拨驱动开关 typec_ucsi.fct_client，不碰 gadget 组合、不碰 qusb。
# flag 出现(AT+QFCT="done")后把开关拨回去并退出，恢复正常 OTG 行为。

FLAG=/var/persist/fct_done_flag
PARAM=/sys/module/typec_ucsi/parameters/fct_client
ROLE=/sys/class/usb_role/a600000.usb-role-switch/role

log() { echo "fct-usb-client: $*"; }

# persist 是独立分区(/dev/sda2)，没挂上来时看不到 flag，不能当成产测模式。
i=0
while ! grep -q " /var/persist " /proc/self/mounts; do
    i=$((i + 1))
    [ "$i" -gt 30 ] && { log "/var/persist not mounted, keep default role"; exit 0; }
    sleep 1
done

[ -e "$FLAG" ] && exit 0

if ! echo 1 > "$PARAM" 2>/dev/null; then
    log "WARN: failed to write $PARAM"
    exit 1
fi
log "fct_client=1"

# 开机就插着线时 UCSI 的事件在本服务起来之前就处理完了，补一次。已经是 device
# 就不写，避免多触发一次 dwc3 core 重建。
[ "$(cat "$ROLE" 2>/dev/null)" = "device" ] || echo device > "$ROLE"

while [ ! -e "$FLAG" ]; do
    sleep 2
done

echo 0 > "$PARAM"
log "fct_done_flag appeared, released"
