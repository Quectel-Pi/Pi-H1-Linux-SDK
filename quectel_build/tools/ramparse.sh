#!/bin/bash
#
# QCS6490 (Quectel QSM565DW) Ramdump 解析包装脚本
# 用法: ./ramparse.sh <dump目录> [vmlinux路径] [额外ramparse参数...]
#
# 流程:
#   1. 检查 Python3 与 pyelftools 依赖
#   2. 自动探测 Yocto aarch64 交叉工具链 (nm/objdump)
#   3. 生成 local_settings.py 指定 gdb/nm 路径
#   4. 调用 ramparse.py --auto-dump 解析 ramdump
#
# 前置条件:
#   - 设备已发生 kernel panic 并在 /var/spool/crash 生成 ramdump
#     (dumpenable 开启时,详见 ramdump-parse-SKILL.md)
#   - 已用 adb pull 将 dump 文件拉到本地
#   - Yocto build 目录存在 (含 vmlinux 和交叉 nm)
#

set -euo pipefail

# ===== 路径 =====
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PARSER_DIR="${SCRIPT_DIR}/linux-ramdump-parser-v2"
RAMPARSE="${PARSER_DIR}/ramparse.py"
LOCAL_SETTINGS="${PARSER_DIR}/local_settings.py"

# 项目根目录 (tools 的上两级)
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build-qcom-wayland"

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

info()  { echo -e "${GREEN}[INFO]${NC}  $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*"; }

# ===== 参数检查 =====
if [[ $# -lt 1 ]]; then
    echo "用法: $0 <dump目录> [vmlinux路径] [额外ramparse参数...]"
    echo ""
    echo "示例:"
    echo "  $0 /tmp/ramdump-Dump_Collection"
    echo "  $0 /tmp/ramdump-Dump_Collection /path/to/vmlinux"
    echo "  $0 /tmp/dump /path/to/vmlinux --64-bit --print-ddr-stats"
    echo ""
    echo "dump 目录应包含:"
    echo "  - RAM dump 文件 (*.elf / *.bin, 文件名含 start-end 地址)"
    echo "  - vmlinux (可选, 没传则从 dump 目录或 build 目录找)"
    exit 1
fi

DUMP_DIR="$1"
shift

# 解析 vmlinux: 命令行参数 > dump 目录内 vmlinux > build 目录 vmlinux
VMLINUX=""
if [[ $# -gt 0 ]] && [[ "${1:-}" != --* ]]; then
    VMLINUX="$1"
    shift
fi

if [[ -z "$VMLINUX" ]] || [[ ! -f "$VMLINUX" ]]; then
    # 尝试 dump 目录内的 vmlinux
    if [[ -f "${DUMP_DIR}/vmlinux" ]]; then
        VMLINUX="${DUMP_DIR}/vmlinux"
    elif [[ -f "${BUILD_DIR}/tmp-glibc/work/qcm6490_idp-qcom-linux/linux-qcom-custom/6.6/build/vmlinux" ]]; then
        VMLINUX="${BUILD_DIR}/tmp-glibc/work/qcm6490_idp-qcom-linux/linux-qcom-custom/6.6/build/vmlinux"
    elif [[ -f "${BUILD_DIR}/tmp-glibc/deploy/images/qcm6490-idp/vmlinux" ]]; then
        VMLINUX="${BUILD_DIR}/tmp-glibc/deploy/images/qcm6490-idp/vmlinux"
    else
        error "找不到 vmlinux，请用第二个参数指定路径"
        error "  $0 <dump目录> <vmlinux路径>"
        exit 1
    fi
    info "自动找到 vmlinux: $VMLINUX"
fi

# ===== 1. 检查 Python3 =====
if ! command -v python3 &>/dev/null; then
    error "找不到 python3，请先安装: sudo apt install python3 python3-pip"
    exit 1
fi

PY_VER=$(python3 -c 'import sys; print("%d.%d" % sys.version_info[:2])')
info "Python: $PY_VER"

# 检查 pyelftools
if ! python3 -c "import elftools" 2>/dev/null; then
    info "安装 Python 依赖 pyelftools..."
    python3 -m pip install --user pyelftools 2>&1 | tail -3 || {
        error "pyelftools 安装失败，请手动执行: pip3 install --user pyelftools"
        exit 1
    }
fi

# ===== 2. 探测工具链 nm / objdump / gdb =====
NM_PATH=""
OBJDUMP_PATH=""

# 优先 Yocto 交叉工具链（find -print -quit 找到即停，避免 pipefail 下 SIGPIPE）
if [[ -d "$BUILD_DIR" ]]; then
    NM_PATH=$(find "$BUILD_DIR/tmp-glibc" -name "aarch64-qcom-linux-nm" -type f -print -quit 2>/dev/null || true)
    OBJDUMP_PATH=$(find "$BUILD_DIR/tmp-glibc" -name "aarch64-qcom-linux-objdump" -type f -print -quit 2>/dev/null || true)
fi

# 回退: 系统 aarch64 工具链
if [[ -z "$NM_PATH" ]]; then
    NM_PATH=$(command -v aarch64-linux-gnu-nm 2>/dev/null || true)
fi
if [[ -z "$OBJDUMP_PATH" ]]; then
    OBJDUMP_PATH=$(command -v aarch64-linux-gnu-objdump 2>/dev/null || true)
fi

# gdb: 优先 gdb-multiarch,其次系统 gdb,最后交叉 gdb
GDB_PATH=""
for g in gdb-multiarch gdb aarch64-linux-gnu-gdb; do
    if command -v "$g" &>/dev/null; then
        GDB_PATH=$(command -v "$g")
        break
    fi
done

if [[ -z "$GDB_PATH" ]]; then
    error "找不到 gdb，请安装: sudo apt install gdb-multiarch"
    exit 1
fi
if [[ -z "$NM_PATH" ]]; then
    error "找不到 aarch64 nm。 请确认 Yocto 已编译 (build-qcom-wayland 目录存在) "
    error "或安装系统交叉工具链: sudo apt install gcc-aarch64-linux-gnu"
    exit 1
fi

info "GDB:    $GDB_PATH"
info "NM:     $NM_PATH"
info "OBJDUMP: ${OBJDUMP_PATH:-未找到(部分 parser 会跳过)}"

# gdb 必须支持 aarch64, 否则 sizeof(void*)/long 会被 gdb 按默认架构错报为 4,
# 导致 read_pointer 丢高 32 位、dmesglib 提取崩溃 (TypeError: None >> int)。
# ramdump.py 已有 arm64 兜底, 但装 gdb-multiarch 是根治。
if "$GDB_PATH" -batch -ex "set architecture aarch64" >/dev/null 2>&1; then
    info "GDB 支持 aarch64: OK"
else
    warn "gdb 不支持 aarch64 (set architecture 失败)!"
    warn "  这会导致 sizeof(void*)=4 错报, read_pointer 丢高 32 位, dmesg 提取崩溃"
    warn "  ramdump.py 已有 arm64 兜底, 但强烈建议安装: sudo apt install gdb-multiarch"
fi

# ===== 3. 生成 local_settings.py =====
info "生成 local_settings.py..."
cat > "$LOCAL_SETTINGS" << EOF
# 自动生成 by ramparse.sh —— 请勿手动编辑（如需固定路径可手工修改）
gdb_path = "${GDB_PATH}"
gdb64_path = "${GDB_PATH}"
nm_path = "${NM_PATH}"
nm64_path = "${NM_PATH}"
objdump_path = "${OBJDUMP_PATH}"
objdump64_path = "${OBJDUMP_PATH}"
qtf_path = ""
scandump_parser_path = ""
cpuss_parser_path = ""
cpuss_parser_json = ""
crashtool = ""
trace_ext = ""
tracecmdtool = ""
EOF

# ===== 4. 输出目录 =====
OUTDIR="${DUMP_DIR}/parsed_output"
mkdir -p "$OUTDIR"

# ===== 5. 固定 QCS6490 参数 + 可选 KASLR offset =====
# QCS6490 的 board 定义在 extensions/board_def.py (socid=475, board_num='6490')
FORCE_HW="6490"

# KASLR dump 修复: 当 banner 匹配失败(报 "your vmlinux is probably wrong")
# 但 DDR 里确实有 banner 时, 需要手动指定 kimage_voffset.
# 推算方法: kimage_voffset = (linux_banner 虚拟地址) - (linux_banner 物理地址)
#   - 虚拟地址: <交叉nm> vmlinux | grep " D linux_banner$"
#   - 物理地址: 在 DDR dump 里 grep -abo "Linux version" 找到 file offset, + 该文件物理基址
# 用法: KIMAGE_VOFFSET=0xffffffbe05200000 ./ramparse.sh <dump> <vmlinux>
KIMAGE_VOFFSET="${KIMAGE_VOFFSET:-}"

# ===== 6. 调用 ramparse.py =====
info "===== 开始解析 Ramdump ====="
info "Dump 目录: $DUMP_DIR"
info "vmlinux:  $VMLINUX"
info "输出目录: $OUTDIR"
[[ -n "$KIMAGE_VOFFSET" ]] && info "KASLR kimage_voffset: $KIMAGE_VOFFSET"
echo ""

# ramparse 核心参数:
#   --auto-dump       自动发现 dump 目录中所有 RAM 文件
#   --vmlinux         带符号的内核镜像
#   --64-bit          arm64 dump (QCS6490)
#   --force-hardware  QCS6490 board (避免 SMEM 自动检测失败)
#   --kimage-voffset  KASLR dump 的内核物理偏移(可选, 见上方推算方法)
#   --outdir          输出目录
#   -x                解析所有 parser (慢但全)
EXTRA=()
[[ -n "$KIMAGE_VOFFSET" ]] && EXTRA+=(--kimage-voffset "$KIMAGE_VOFFSET")
set -- "$@" --auto-dump "$DUMP_DIR" --vmlinux "$VMLINUX" --64-bit \
    --force-hardware "$FORCE_HW" "${EXTRA[@]}" --outdir "$OUTDIR"

info "执行命令:"
info "  python3 ${RAMPARSE##*/} $*"
echo ""

if python3 "$RAMPARSE" "$@"; then
    RET=0
else
    RET=$?
fi
echo ""
if [[ $RET -eq 0 ]]; then
    info "===== 解析完成 ====="
    info "结果输出在: $OUTDIR"
    info "关键文件:"
    info "  dmesg_TZ.txt          - 内核 panic 时的 dmesg / 日志"
    info "  tasks.txt              - 各任务内核栈(含符号)"
    info "  tasks_highlight.txt    - 高亮的任务栈"
    info "  taskdump.txt           - 任务转储"
    info "  runqueue* / irqstate*  - 运行队列/中断状态"
    info "  watchdog*              - 看门狗信息"
    info "  vmstats.txt            - 内存统计"
    info "  thermal_info/          - 温度信息"
    info "  uevent.txt             - uevent 事件"
else
    error "===== 解析失败 (退出码: $RET) ====="
    error "常见原因:"
    error "  1. vmlinux 与 ramdump 内核版本不匹配"
    error "  2. KASLR dump: banner 匹配失败 -> 用 KIMAGE_VOFFSET 重跑"
    error "     推算: KIMAGE_VOFFSET=0x<banner_va - banner_phys> $0 <dump> <vmlinux>"
    error "  3. ramdump 文件不完整"
    error "  4. gdb 版本过旧不支持 aarch64"
fi

exit $RET
