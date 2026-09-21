#!/bin/bash
# ============================================================
# Quectel PI H1 (QCS6490) Yocto SDK 全盘烧录脚本
# 用法: ./scripts/flash.sh [ufs|emmc]     默认 ufs
#
# 平台分派:
#   Linux (非 WSL) -> qdl (依赖 libusb + udev)
#   WSL2           -> 板子 USB 接在 Windows 主机, WSL2 默认不做 USB 直通,
#                     所以转发到 Windows 侧 QFIL 后端 (tools/qfil + scripts/flash.bat)。
#                     若已用 usbipd-win 直通 (WSL 内可见 9008) 则直接用 qdl;
#                     设 QPI_FLASH_LOCAL=1 可强制用 qdl。
#   Windows (MSYS) -> QFIL 后端 (fh_loader + QSaharaServer), 走 scripts/flash.bat
#   macOS          -> 不支持 (无 udev, 且 quectel_build/tools/qdl 为 Linux ELF)
#
# 固件目录: quectel_build/<QUECTEL_PROJECT_REV>/  (由 buildpackage 生成)
#   rev 从 quectel-buildconfig-gen.h 读取, 与 tools/flash.sh 同源逻辑。
#   可用 QPI_FW_DIR 覆盖。
# ============================================================
set -euo pipefail
cd "$(dirname "$0")/.."
SDK_ROOT="$(pwd)"

# 构建配置头 (记录 QUECTEL_PROJECT_REV)
GEN_H="${SDK_ROOT}/quectel_build/compile/quectel-features-config/quectel-buildconfig-gen.h"
# 工具目录 (qdl / adb 都在这里)
TOOLS_DIR="${SDK_ROOT}/quectel_build/tools"

# ---------------------------------------------------------------------------
# 是否运行在 WSL 内 (三重探测: 环境变量 / /proc/version / osrelease)
# ---------------------------------------------------------------------------
_qpi_is_wsl() {
    [ -n "${WSL_DISTRO_NAME:-}" ] && return 0
    grep -qi "microsoft" /proc/version 2>/dev/null && return 0
    grep -qi "microsoft" /proc/sys/kernel/osrelease 2>/dev/null && return 0
    return 1
}

# 转发到 Windows 侧的 scripts/flash.bat (MSYS 与 WSL 共用)
#   SDK 位于 /mnt/<盘符> 时 bat 与固件是普通 Windows 路径;
#   位于 WSL ext4 时经 UNC (\\wsl.localhost\...) 访问 —— bat 里用 pushd
#   (支持 UNC, 会自动映射临时盘符), 实测读取远快于 USB 烧录, 不构成瓶颈,
#   因此无需预先复制固件。
_qpi_flash_via_bat() {
    local bat
    bat="${SDK_ROOT}/scripts/flash.bat"
    [ -f "${bat}" ] || { echo "[ERROR] 找不到 Windows 烧录脚本: ${bat}"; exit 1; }
    [ -d "${SDK_ROOT}/tools/qfil" ] || {
        echo "[ERROR] 缺少 QFIL 后端目录: ${SDK_ROOT}/tools/qfil"; exit 1; }

    local cmd_exe="" c
    for c in /mnt/c/Windows/System32/cmd.exe cmd.exe; do
        if command -v "${c}" >/dev/null 2>&1; then cmd_exe="${c}"; break; fi
    done
    if [ -z "${cmd_exe}" ]; then
        echo "[ERROR] 无法调用 cmd.exe (WSL interop 未启用?)"
        echo "        请在 Windows 侧手动执行: scripts\\flash.bat ${1:-ufs}"
        exit 1
    fi

    local win_bat="${bat}"
    if command -v wslpath >/dev/null 2>&1; then
        win_bat="$(wslpath -w "${bat}" 2>/dev/null || echo "${bat}")"
    elif command -v cygpath >/dev/null 2>&1; then
        win_bat="$(cygpath -w "${bat}")"
    fi

    # WSL 里 export 的变量不会自动进入 Windows 进程。
    # WSLENV 是 WSL 官方的变量桥接机制: 把名字加进去即可透传。
    # (实测: 不列入 WSLENV 时 bat 读到的是空值)
    local bridged="${WSLENV:-}" v
    for v in QPI_FW_DIR QPI_NO_RESET QPI_QFIL_DIR; do
        eval "local val=\${$v:-}"
        [ -n "${val}" ] || continue
        case ":${bridged}:" in
            *":${v}:"*) ;;
            *) bridged="${bridged:+${bridged}:}${v}" ;;
        esac
    done

    # cmd.exe 的初始工作目录若为 UNC 路径, 会打印
    # "UNC 路径不受支持。默认值设为 Windows 目录" 警告(GBK 编码, 在 WSL 里显示为乱码),
    # 极易被误认为错误。bat 内部用 %~dp0 / pushd 自行定位, 与 cwd 无关,
    # 所以这里先切到一个 Windows 可达目录把警告消掉。
    if [ -d /mnt/c ]; then cd /mnt/c; fi

    exec env MSYS_NO_PATHCONV=1 WSLENV="${bridged}" \
        "${cmd_exe}" /c "${win_bat}" "$@"
}

case "$(uname -s 2>/dev/null)" in
    MINGW*|MSYS*|CYGWIN*)
        echo "[h1-yocto] 检测到 Windows 环境, 切换为 QFIL 后端烧录"
        _qpi_flash_via_bat "$@"
        ;;
    Linux)
        if _qpi_is_wsl && [ "${QPI_FLASH_LOCAL:-0}" != "1" ]; then
            if command -v lsusb >/dev/null 2>&1 && lsusb 2>/dev/null | grep -q "05c6:9008"; then
                echo "[h1-yocto] WSL 内已直通 9008 设备, 使用 qdl 烧录"
            else
                echo "[h1-yocto] 检测到 WSL2 环境"
                echo "          板子 USB 接在 Windows 主机上, WSL2 默认不做 USB 直通"
                echo "          -> 转发到 Windows 侧 QFIL 后端 (scripts/flash.bat)"
                echo "          (若已在 WSL 内用 usbipd-win 直通设备, 设 QPI_FLASH_LOCAL=1 改用 qdl)"
                echo
                _qpi_flash_via_bat "$@"
            fi
        fi
        ;;
    Darwin)
        echo "[ERROR] macOS 不支持本 SDK 烧录 (需 udev/libusb, 且 quectel_build/tools/qdl 为 Linux ELF)"
        echo "        请在 Linux 或 WSL2 内运行, 或使用 Windows 下的 scripts/flash.bat"
        exit 1
        ;;
esac

# ---------------------------------------------------------------------------
# 以下为 Linux/qdl 路径 (非 WSL, 或 WSL 已 usbipd 直通)
# ---------------------------------------------------------------------------
FS_TYPE="${1:-ufs}"

# 固件目录: 优先 QPI_FW_DIR, 否则从 buildconfig 配置头取 QUECTEL_PROJECT_REV
if [ -n "${QPI_FW_DIR:-}" ]; then
    FW_DIR="${QPI_FW_DIR}"
else
    [ -f "${GEN_H}" ] || {
        echo "[ERROR] 找不到构建配置: ${GEN_H}"
        echo "        请先在插件/终端执行 buildconfig (配置构建环境)"
        exit 1
    }
    FW_REV="$(sed -n 's/^[[:space:]]*#define[[:space:]]\+QUECTEL_PROJECT_REV[[:space:]]\+"\([^"]*\)".*/\1/p' "${GEN_H}" | head -n1)"
    [ -n "${FW_REV}" ] || { echo "[ERROR] 无法从 ${GEN_H} 解析 QUECTEL_PROJECT_REV"; exit 1; }
    FW_DIR="${SDK_ROOT}/quectel_build/${FW_REV}"
fi

QDL="${TOOLS_DIR}/qdl"
SUDO="${SUDO:-sudo}"

# 固件完整性检查 (与 buildpackage 产物对应)
for f in prog_firehose_Qcm6490_ddr.elf efi.bin system.img dtb.bin; do
    [ -f "${FW_DIR}/${f}" ] || {
        echo "[ERROR] 固件缺少 ${f}"
        echo "        目录: ${FW_DIR}"
        echo "        请先执行全量编译 + buildpackage (点击「全量编译」)"
        exit 1
    }
done
[ -d "${FW_DIR}/partition_${FS_TYPE}" ] || {
    echo "[ERROR] 缺少 partition_${FS_TYPE} 目录 (固件目录: ${FW_DIR})"; exit 1; }

echo "=========================================="
echo "[h1-yocto] 全盘烧录固件 (${FS_TYPE})"
echo "  固件目录: ${FW_DIR}"
echo "  模式: 全 LUN 烧录 (rawprogram0-5 + patch0-5)"
echo "=========================================="

# 1. 等待 EDL (9008) 设备 (最多 60s, 供用户手动进 EDL)
echo "[h1-yocto] 等待 9008 EDL 设备 (若未进 EDL: adb shell reboot edl, 或断电重上电按组合键)..."
in_edl=0
for i in $(seq 1 60); do
    if lsusb 2>/dev/null | grep -q "05c6:9008"; then
        echo "[h1-yocto] 已检测到 9008 EDL 设备 ✓"
        in_edl=1
        break
    fi
    [ "$i" -eq 60 ] && { echo "[ERROR] 等待 9008 超时 (60s)。请确认设备已进 EDL。"; exit 1; }
    sleep 1
done

# 2. 全盘烧录 (所有 LUN 的 rawprogram + patch)
cd "${FW_DIR}"
echo "[h1-yocto] qdl 全盘烧录中..."
# 显式列出 0-5 全部 LUN (保证顺序, 不依赖 glob 顺序)
XMLS=()
for lun in 0 1 2 3 4 5; do
    XMLS+=(partition_${FS_TYPE}/rawprogram${lun}.xml partition_${FS_TYPE}/patch${lun}.xml)
done
${SUDO} "${QDL}" -s "${FS_TYPE}" -i . \
    "prog_firehose_Qcm6490_ddr.elf" \
    "${XMLS[@]}"

echo ""
echo "[h1-yocto] 全盘烧录完成 ✓"
echo "  请断电重新上电启动设备"
