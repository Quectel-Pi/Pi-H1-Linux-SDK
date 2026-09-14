#!/bin/sh
# Copyright (C) 2026 Quectel Igni.Li
# Licensed on MIT
#
# TimeCapsule 内核/脚本安装助手。
# 由 GUI 以 root (sudo) 身份调用, 完成需要写 /opt 的操作并自检。
#
# 用法:
#   ota_kernel_install <efi_file>
#       把 EFI 内核 + 自动计算的 MD5 放入 /opt/kernel_upgrade/ 并 md5sum -c 自检。
#   ota_kernel_install install-script <script_file>
#       替换 /opt/system_upgrade.sh (原文件备份为 .bak.<时间戳>)。
#
# 输出: 成功 0; 失败 非 0 + stderr 说明。
# 设计: sudoers 白名单内的单一命令, 最小提权面。

set -e

DEST_DIR="/opt/kernel_upgrade"
DEST_EFI="$DEST_DIR/linux-qcm6490-idp.efi"
DEST_MD5="$DEST_DIR/linux-qcm6490-idp.efi.md5"
UPGRADE_SCRIPT="/opt/system_upgrade.sh"

mode="kernel"
EFI_SRC="$1"
if [ "$1" = "install-script" ]; then
    mode="script"
    EFI_SRC="$2"
fi

if [ -z "$EFI_SRC" ]; then
    echo "ERROR: 用法: ota_kernel_install <efi_file> | ota_kernel_install install-script <script>" >&2
    exit 2
fi
if [ ! -f "$EFI_SRC" ]; then
    echo "ERROR: 文件不存在: $EFI_SRC" >&2
    exit 2
fi

if [ "$mode" = "script" ]; then
    # 替换升级脚本 (备份原文件)
    if [ -f "$UPGRADE_SCRIPT" ]; then
        BAK="$UPGRADE_SCRIPT.bak.$(date +%Y%m%d%H%M%S)"
        cp -af "$UPGRADE_SCRIPT" "$BAK"
        echo "[INFO] 原升级脚本已备份: $BAK"
    fi
    cp -af "$EFI_SRC" "$UPGRADE_SCRIPT"
    chmod 755 "$UPGRADE_SCRIPT"
    sync
    echo "[OK] 升级脚本已更新: $UPGRADE_SCRIPT"
    exit 0
fi

# ---- 内核安装 ----
# 基本健全性: 内核 EFI 通常远大于 1MB
SIZE=$(stat -c %s "$EFI_SRC" 2>/dev/null || stat -f %z "$EFI_SRC")
if [ "$SIZE" -lt 1048576 ]; then
    echo "WARN: 文件仅 ${SIZE} 字节, 偏小, 但继续" >&2
fi

mkdir -p "$DEST_DIR"

echo "[INFO] 复制 EFI -> $DEST_EFI"
cp -af "$EFI_SRC" "$DEST_EFI"
chmod 644 "$DEST_EFI"

echo "[INFO] 计算 MD5 ..."
MD5=$(md5sum "$DEST_EFI" | awk '{print $1}')
# md5sum 校验文件格式: "<md5>  <basename>" (注意两个空格)
printf "%s  %s\n" "$MD5" "$(basename "$DEST_EFI")" > "$DEST_MD5"
chmod 644 "$DEST_MD5"
sync

echo "[INFO] 自检 md5sum -c ..."
( cd "$DEST_DIR" && md5sum -c "$(basename "$DEST_MD5")" )
echo "[OK] 内核升级文件已就绪: $DEST_EFI ($SIZE bytes, md5:$MD5)"

