#!/bin/bash
#
# QCS6490 (Quectel QSM565DW) 一键烧录脚本
# 用法: ./flash.sh [ufs|emmc]
#   默认使用 UFS 存储类型
#
# 流程:
#   1. 检查 ADB 设备连接
#   2. 通过 adb shell reboot edl 进入 9008 EDL 模式
#      （Debian/Ubuntu 桌面镜像登录着 GNOME 时，logind 会拦 reboot，
#        此时自动改用 systemctl reboot -i --reboot-argument=edl 绕过抑制剂）
#   3. 等待 Qualcomm USB 9008 设备出现
#   4. 使用 qdl 烧录固件
#

set -euo pipefail

# ===== 配置 =====
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_CONFIG="${SCRIPT_DIR}/../compile/quectel-features-config/quectel-buildconfig-gen.h"

# 从配置文件读取固件目录名
if [[ -f "$BUILD_CONFIG" ]]; then
    FW_REV=$(grep 'QUECTEL_PROJECT_REV' "$BUILD_CONFIG" | sed 's/.*"\(.*\)".*/\1/')
    FW_DIR="${SCRIPT_DIR}/../${FW_REV}"
    # 调试版固件打包到 "<版本号>_DBG"，与 a_key_generation.sh 的规则保持一致
    FW_CUSTOM=$(grep 'QUECTEL_CUSTOM_NAME' "$BUILD_CONFIG" | sed 's/.*"\(.*\)".*/\1/' || true)
    case "/${FW_CUSTOM}/" in
        */DBG/*) FW_DIR="${FW_DIR}_DBG" ;;
    esac
else
    error "找不到配置文件: $BUILD_CONFIG"
    exit 1
fi

STORAGE="${1:-ufs}"           # 默认 UFS，可传 emmc
# UFS 的 rawprogram/patch XML 与固件根目录下的同名文件逐字节相同，且 qdl 用 -i 指定的
# 目录优先解析 XML 里的裸文件名，所以下面的 .bin 实际一直是从固件根目录取的
# （partition_ufs/*.bin 从未被读到）。打包时不再带 partition_ufs/，UFS 直接从根目录读。
PART_DIR="$FW_DIR"
if [[ "$STORAGE" != "ufs" ]]; then
    PART_DIR="$FW_DIR/partition_${STORAGE}"
fi
EDL_TIMEOUT=30               # 等待 9008 设备的超时秒数
SKIP_ENTER_EDL=0             # 已在 EDL 模式时跳过 reboot

# 优先使用 tools 目录下的本地二进制，找不到再用系统全局的
if [[ -x "${SCRIPT_DIR}/qdl" ]]; then
    QDL_BIN="${SCRIPT_DIR}/qdl"
else
    QDL_BIN="qdl"
fi
if [[ -x "${SCRIPT_DIR}/adb" ]]; then
    ADB_BIN="${SCRIPT_DIR}/adb"
else
    ADB_BIN="adb"
fi

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

info()  { echo -e "${GREEN}[INFO]${NC}  $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*"; }

# ===== 前置检查 =====
# 检查并安装系统依赖
install_deps() {
    local pkgs=()
    local needed=0

    # 检查 qdl 运行时依赖
    for lib in libusb-1.0 libxml2 libzip; do
        if ! ldconfig -p 2>/dev/null | grep -q "${lib}"; then
            needed=1
            break
        fi
    done

    # 检查 lsusb (usbutils)
    if ! command -v lsusb &>/dev/null; then
        needed=1
    fi

    if [[ $needed -eq 1 ]]; then
        info "检测到缺少系统依赖，准备安装..."
        # 构建包列表
        for pkg in libusb-1.0-0 libxml2-dev libzip-dev usbutils; do
            if ! dpkg -s "$pkg" &>/dev/null 2>&1; then
                pkgs+=("$pkg")
            fi
        done

        if [[ ${#pkgs[@]} -gt 0 ]]; then
            info "需要安装: ${pkgs[*]}"
            if command -v sudo &>/dev/null; then
                sudo apt update -qq && sudo apt install -y "${pkgs[@]}"
            else
                error "需要 root 权限安装依赖，请手动执行:"
                error "  apt install -y ${pkgs[*]}"
                exit 1
            fi
        fi
        info "依赖检查完成"
    fi
}

check_deps() {
    local missing=0
    for cmd in "$ADB_BIN" "$QDL_BIN"; do
        if ! command -v "$cmd" &>/dev/null && [[ ! -x "$cmd" ]]; then
            error "找不到 $cmd，请先安装"
            missing=1
        fi
    done
    if [[ $missing -eq 1 ]]; then
        exit 1
    fi
}

check_fw_dir() {
    if [[ ! -d "$FW_DIR" ]]; then
        error "固件目录不存在: $FW_DIR"
        exit 1
    fi

    if [[ ! -f "$FW_DIR/prog_firehose_Qcm6490_ddr.elf" ]]; then
        error "Firehose programmer 不存在: $FW_DIR/prog_firehose_Qcm6490_ddr.elf"
        exit 1
    fi

    local partition_dir="$PART_DIR"
    if [[ ! -d "$partition_dir" ]]; then
        error "分区目录不存在: $partition_dir"
        exit 1
    fi

    # 检查是否有 rawprogram 和 patch 文件
    if ! ls "$partition_dir"/rawprogram*.xml &>/dev/null; then
        error "找不到 rawprogram*.xml 在 $partition_dir"
        exit 1
    fi
    if ! ls "$partition_dir"/patch*.xml &>/dev/null; then
        error "找不到 patch*.xml 在 $partition_dir"
        exit 1
    fi
}

# ===== 检查 ADB 设备 =====
check_adb_device() {
    # 启动 adb server
    "$ADB_BIN" start-server 2>/dev/null || true

    local device_count
    device_count=$("$ADB_BIN" devices 2>/dev/null | grep -cw "device$" || true)

    if [[ "$device_count" -eq 0 ]]; then
        # 没有 ADB 口：检查是否已在 EDL (9008) 模式，有则直接烧录
        if lsusb 2>/dev/null | grep -qiE "05c6:9008|05c6:9018"; then
            info "未检测到 ADB 设备，但发现 9008/9018 EDL 设备，直接烧录"
            SKIP_ENTER_EDL=1
            return 0
        fi
        error "没有检测到 ADB 设备，也没有发现 9008 EDL 设备"
        error "请确认：USB 连接正常，或设备已进入 EDL 模式（断电重上电可触发 EDL）"
        "$ADB_BIN" devices
        exit 1
    elif [[ "$device_count" -gt 1 ]]; then
        warn "检测到多个 ADB 设备，将使用第一个"
        "$ADB_BIN" devices | grep "device$" | head -1
    fi

    info "ADB 设备已连接"
}

# ===== 进入 EDL 模式 =====
# 板端的 /sbin/reboot 是 systemctl 的软链（Debian/Ubuntu 的 systemd-sysv 包，
# sysvinit-core 与之互斥），所以 `reboot edl` 等价于 `systemctl reboot edl`：
# "edl" 作为 reboot(2) 的参数传下去，内核按 DT 的 psci/mode-edl
# (arch/arm64/boot/dts/qcom/qcm6490.dtsi: mode-edl = <0 0x1>) 走
# PSCI SYSTEM_RESET2 真正进 9008。
#
# 但登录了桌面会话（gdm3 + gnome-session-b）时，logind 会拦：
#   Operation inhibited by "p" (PID ... "gnome-session-b", ...)
#   User p is logged in on tty2.
# 这时必须加 -i (--check-inhibitors=no) 才放行，参数照样传下去。
#
# 还有一个坑：`systemctl reboot edl` 是错的！reboot 这个 verb 的 max_args=1
# (src/systemctl/systemctl.c 的 verb 表)，多一个位置参数直接报
# "Too many arguments." 并退出，压根不会重启。要显式指定就写：
#   systemctl reboot --reboot-argument=edl
# `reboot edl` 能用，是因为 argv[0]=="reboot" 走的是 halt_parse_argv()
# 兼容解析 (src/systemctl/systemctl-compat-halt.c:142 允许一个位置参数)。
#
# 注意：quectel_build/tools/adb 是 28.0.2，不支持 shell v2 协议，
# `adb shell` 的退出码恒为 0（板端失败也返回 0），所以这里一律抓板端
# 回显判断结果，不能靠 $?。板端 pty 把 stderr 也并进 stdout，2>/dev/null
# 也挡不住，必须显式读出来。
edl_reboot_attempt() {
    local cmd="$1" out=""

    info "发送: adb shell ${cmd}"
    out=$("$ADB_BIN" shell "$cmd" 2>&1 || true)

    if grep -qiE 'inhibited|logged in on|retry operation after' <<<"$out"; then
        warn "板端拒绝了重启（logind 抑制剂）:"
        sed 's/^/    /' <<<"$out"
        return 2        # 可重试：换更强的参数
    fi
    if grep -qiE 'must be superuser|permission denied|not permitted|Operation not permitted' <<<"$out"; then
        error "板端权限不足:"
        sed 's/^/    /' <<<"$out"
        return 3        # 不可重试
    fi
    if grep -qiE 'not found|No such file or directory' <<<"$out"; then
        error "板端没有该命令:"
        sed 's/^/    /' <<<"$out"
        return 3        # 不可重试
    fi

    [ -n "$out" ] && sed 's/^/    /' <<<"$out"
    return 0
}

enter_edl() {
    # 设备已在 EDL 模式时（check_adb_device 检测到 9008/9018），跳过 reboot
    if [[ "${SKIP_ENTER_EDL:-0}" -eq 1 ]]; then
        info "设备已在 EDL 模式，跳过 reboot"
        return 0
    fi

    # 依次尝试：原路径 → 跳过抑制剂 → 跳过 logind 直落底层
    # 注意第 2/3 条不能写成 `systemctl reboot -i edl`（Too many arguments）
    local -a attempts=(
        "reboot edl"
        "systemctl reboot -i --reboot-argument=edl"
        "systemctl reboot -i --force --reboot-argument=edl"
    )
    local i rc=0
    for (( i = 0; i < ${#attempts[@]}; i++ )); do
        rc=0
        edl_reboot_attempt "${attempts[i]}" || rc=$?
        if [[ $rc -eq 0 ]]; then
            break
        elif [[ $rc -eq 3 ]]; then
            error "无法进入 EDL 模式，请确认:"
            error "  1. adb shell 有 root 权限（systemctl reboot 需要）"
            error "  2. 或断电重上电 / 手动按键进入 EDL 模式"
            exit 1
        fi
    done

    if [[ $rc -ne 0 ]]; then
        error "无法进入 EDL 模式：板端 logind 抑制剂一直在拦 reboot"
        error "板上查看还有谁在拦: systemd-inhibit --list"
        error "登录中的会话:   loginctl list-sessions"
        error "临时放行:       loginctl terminate-user <user>"
        error "或者直接断电重上电进 EDL（本脚本会自动识别 9008 并跳过 reboot）"
        exit 1
    fi

    info "等待设备进入 EDL 模式..."
    sleep 3
}

# ===== 等待 9008 设备出现 =====
wait_for_edl() {
    local elapsed=0
    info "等待 Qualcomm 9008 USB 设备出现 (超时: ${EDL_TIMEOUT}s)..."

    while [[ $elapsed -lt $EDL_TIMEOUT ]]; do
        # 检查 USB 设备: vendor 05c6 (Qualcomm), product 9008 (EDL)
        if lsusb 2>/dev/null | grep -qi "05c6:9008"; then
            info "检测到 9008 设备！"
            return 0
        fi
        # 也检查 9018 (某些设备 EDL 的 PID)
        if lsusb 2>/dev/null | grep -qi "05c6:9018"; then
            info "检测到 9018 设备 (EDL 模式)！"
            return 0
        fi
        sleep 1
        elapsed=$((elapsed + 1))
        # 每 5 秒打印一次等待提示
        if (( elapsed % 5 == 0 )); then
            info "  等待中... (${elapsed}s/${EDL_TIMEOUT}s)"
        fi
    done

    error "超时：未检测到 9008/9018 设备"
    error "请检查:"
    error "  1. USB 线是否连接"
    error "  2. 设备是否真的重启了 —— dmesg 里看 reboot(2) 是否被拦，"
    error "     桌面镜像登录着 GNOME 时 flash 会自动换用 systemctl reboot -i edl"
    error "  3. 设备是否进入了 EDL 模式 (LED 闪烁等)"
    error "  4. udev 规则是否正确 (lsusb 查看)"
    lsusb 2>/dev/null | grep -i "05c6" || warn "未找到 Qualcomm USB 设备"
    exit 1
}

# ===== 执行烧录 =====
do_flash() {
    local partition_dir="$PART_DIR"
    local prog="$FW_DIR/prog_firehose_Qcm6490_ddr.elf"

    info "===== 开始烧录 ====="
    info "固件目录: $FW_DIR"
    info "存储类型: $STORAGE"
    info "Firehose: $(basename "$prog")"

    # 收集 rawprogram 和 patch XML (按文件名排序)
    # 排除 WIPE_PARTITIONS 和 BLANK_GPT，保留 persist 分区
    local rawprogram_files=()
    local patch_files=()

    while IFS= read -r f; do
        rawprogram_files+=("$f")
    done < <(ls "$partition_dir"/rawprogram*.xml 2>/dev/null \
             | grep -v WIPE_PARTITIONS | grep -v BLANK_GPT | sort)

    while IFS= read -r f; do
        patch_files+=("$f")
    done < <(ls "$partition_dir"/patch*.xml 2>/dev/null | sort)

    info "Rawprogram 文件: ${#rawprogram_files[@]} 个 (已排除 WIPE/BLANK_GPT)"
    info "Patch 文件: ${#patch_files[@]} 个"

    # 构建 qdl 命令
    local cmd=("$QDL_BIN")
    cmd+=(-s "$STORAGE")     # 存储类型
    cmd+=(-i "$FW_DIR")      # 搜索固件文件的目录
    cmd+=("$prog")            # firehose programmer

    # 添加所有 rawprogram 和 patch XML
    for f in "${rawprogram_files[@]}" "${patch_files[@]}"; do
        cmd+=("$f")
    done

    info "执行命令:"
    info "  ${cmd[*]}"
    echo ""

    # 执行烧录
    if "${cmd[@]}"; then
        echo ""
        info "===== 烧录完成 ====="
        info "设备将自动重启，请等待系统启动"
    else
        local ret=$?
        echo ""
        error "===== 烧录失败 (退出码: $ret) ====="
        error "常见原因:"
        error "  1. 设备不在 EDL 模式"
        error "  2. USB 连接不稳定"
        error "  3. 固件文件不完整"
        exit $ret
    fi
}

# ===== 主流程 =====
main() {
    echo "=========================================="
    echo "  QCS6490 (Quectel QSM565DW) 一键烧录"
    echo "=========================================="
    echo ""

    install_deps
    check_deps
    check_fw_dir
    check_adb_device
    enter_edl
    wait_for_edl
    do_flash
}

main "$@"
