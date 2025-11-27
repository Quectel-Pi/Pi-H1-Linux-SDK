#!/bin/sh
MOUNT_PREFIX="/var/rootdirs/mnt/usb-"
LOG_FILE="/var/log/usb-mount.log"
DEV_PATTERN="/dev/sd[g-z]1"

# 日志函数
log() {
    echo "[$(date +'%Y-%m-%d %H:%M:%S')] $1" >> $LOG_FILE
}

log "USB多设备监控脚本启动（节点监控版）"

# 杀死占用进程（通用逻辑）
kill_occupied_process() {
    local mount_dir=$1
    local pids=$(fuser -m $mount_dir 2>/dev/null)
    if [ -n "$pids" ]; then
        log "杀死占用进程：$pids（目录：$mount_dir）"
        kill -9 $pids 2>/dev/null
        sleep 1
    fi
}

# 核心：根据文件系统类型生成挂载参数
get_mount_params() {
    local dev=$1
    local fs_type=$2
    local params=""

    case $fs_type in
        "vfat")
            params="-t vfat -o rw,umask=000,shortname=mixed"
            ;;
        "exfat")
            params="-t exfat -o rw,umask=000,allow_other"
            ;;
        "ntfs")
            params="-t ntfs-3g -o rw,umask=000,defaults"
            ;;
        "ext4")
            params="-t ext4 -o rw,defaults"
            ;;
        *)
            params=""
            log "不支持的文件系统：$fs_type（设备：$dev）"
            ;;
    esac
    echo "$params"
}

# 挂载单个设备（复用逻辑，新增/已存在设备通用）
mount_device() {
    local dev=$1
    local dev_name=$(basename "$dev")
    local mount_dir="${MOUNT_PREFIX}${dev_name}"

    # 跳过已挂载（通过/proc/mounts检测，避免重复挂载）
    if grep -qs "$mount_dir" /proc/mounts; then
        log "设备已挂载，跳过：$dev → $mount_dir"
        return
    fi

    # 获取文件系统类型并挂载
    local fs_type=$(blkid -o value -s TYPE "$dev" 2>/dev/null || echo "unknown")
    local mount_params=$(get_mount_params "$dev" "$fs_type")
    if [ -z "$mount_params" ]; then
        return
    fi

    # 执行挂载
    mkdir -p "$mount_dir"
    log "尝试挂载（$2触发）：$mount_params $dev $mount_dir"  # $2传入触发类型（开机/插入）
    if timeout 15 sh -c "mount $mount_params $dev $mount_dir"; then
        if [ "$(ls -A "$mount_dir" 2>/dev/null)" != "" ]; then
            log "挂载成功：$dev（$fs_type）→ $mount_dir"
        else
            log "挂载成功但目录空：$dev → $mount_dir"
        fi
    else
        log "挂载失败：$dev（参数：$mount_params）"
        umount "$mount_dir" 2>/dev/null
        rmdir "$mount_dir" 2>/dev/null
    fi
}

# 卸载单个设备（仅在节点消失时执行）
unmount_device() {
    local dev_name=$1
    local dev_path="/dev/$dev_name"
    local mount_dir="${MOUNT_PREFIX}${dev_name}"

    # 节点已消失且目录存在，才执行卸载
    if ! [ -b "$dev_path" ] && [ -d "$mount_dir" ]; then
        kill_occupied_process "$mount_dir"
        # 同步缓存，确保数据写入
        sync
        sleep 1

        # 卸载重试
        local umount_attempt=0
        while [ $umount_attempt -lt 3 ]; do
            if umount -l "$mount_dir"; then
                rmdir "$mount_dir" 2>/dev/null
                log "卸载成功（拔出触发）：$mount_dir"
                break
            else
                umount_attempt=$((umount_attempt + 1))
                log "卸载重试 $umount_attempt/3：$mount_dir"
                sleep 1
            fi
        done

        if [ $umount_attempt -eq 3 ]; then
            log "卸载失败：$mount_dir（建议手动执行 umount $mount_dir）"
        fi
    fi
}

# 初始化：记录当前已存在的USB设备节点
get_current_devices() {
    # 列出所有匹配的设备节点，去重后输出
    ls -1d $DEV_PATTERN 2>/dev/null | sort | uniq
}
previous_devices=$(get_current_devices)

log "初始化完成，当前已存在的USB设备：$previous_devices"

# ===================== 新增：挂载开机已存在的U盘 =====================
if [ -n "$previous_devices" ]; then
    log "开始挂载开机已存在的USB设备..."
    for dev in $previous_devices; do
        mount_device "$dev" "开机"  # 传入“开机”作为触发类型，日志更清晰
    done
fi
# =====================================================================

# 核心循环：监控节点变化（插入/拔出）
while true; do
    # 获取当前设备列表
    current_devices=$(get_current_devices)

    # 1. 检测新插入设备（当前有但之前没有的节点）
    new_devices=$(comm -13 <(echo "$previous_devices") <(echo "$current_devices"))
    if [ -n "$new_devices" ]; then
        for dev in $new_devices; do
            mount_device "$dev" "插入"  # 传入“插入”作为触发类型
        done
    fi

    # 2. 检测已拔出设备（之前有但当前没有的节点）
    removed_devices=$(comm -23 <(echo "$previous_devices") <(echo "$current_devices"))
    if [ -n "$removed_devices" ]; then
        for dev in $removed_devices; do
            # 提取设备名（如/dev/sdi1 → sdi1）
            dev_name=$(basename "$dev")
            unmount_device "$dev_name"
        done
    fi

    # 更新上一次设备列表，等待下一轮检测
    previous_devices=$current_devices
    sleep 3
done
