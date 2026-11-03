#!/bin/sh
# U 盘/TF 卡自动只读挂载。
# 原来这段逻辑写在 atcid 的 AT+CARDGET 里，只有发 AT 才会挂载；这里改成系统
# 常驻服务，上电/插卡即自动挂到 /media/<分区名>，AT+CARDGET 只负责查询。
# 已被桌面 udisks 挂载的分区 MOUNTPOINT 非空，会直接跳过，不会重复挂载。

log() { echo "[usb-automount] $*"; }

mounted_at() {
    awk -v m="$1" '$2 == m { found = 1 } END { exit !found }' /proc/mounts
}

# 有文件系统、可移动(removable)、且还没挂载的分区 -> 只读挂到 /media/<name>
scan_mount() {
    lsblk -rno NAME,RM,TYPE,FSTYPE,MOUNTPOINT 2>/dev/null |
    awk '$2 == 1 && $3 == "part" && $4 != "" && $5 == "" { print $1 }' |
    while read -r name; do
        [ -b "/dev/$name" ] || continue
        mnt="/media/$name"
        mkdir -p "$mnt" || continue
        if mount -o ro "/dev/$name" "$mnt" 2>/dev/null; then
            log "mounted /dev/$name -> $mnt"
        else
            rmdir "$mnt" 2>/dev/null
        fi
    done
}

# /media 下挂了但设备已经拔掉的挂载点 -> 卸载并清理目录
scan_umount() {
    for mnt in /media/*; do
        [ -d "$mnt" ] || continue
        if ! mounted_at "$mnt"; then
            rmdir "$mnt" 2>/dev/null
            continue
        fi
        src=$(awk -v m="$mnt" '$2 == m { print $1; exit }' /proc/mounts)
        [ -b "$src" ] || {
            umount -l "$mnt" 2>/dev/null && log "unmounted stale $mnt"
            rmdir "$mnt" 2>/dev/null
        }
    done
}

log "started"
# 稍等再首轮扫描，给桌面环境的 udisks 自动挂载留出先后顺序
sleep 3
while :; do
    scan_mount
    scan_umount
    sleep 2
done
