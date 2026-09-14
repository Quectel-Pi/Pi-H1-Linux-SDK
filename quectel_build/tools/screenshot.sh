#!/bin/bash
# QCS6490 屏幕截图工具（GNOME Wayland）
# 用法: screenshot.sh [输出文件名]
#
# 支持两种场景：
#   1. GDM greeter 阶段（gnome-initial-setup 用户）
#   2. 正常用户会话阶段（已登录用户）

SDK_ROOT=$(git rev-parse --show-toplevel 2>/dev/null || echo ".")
LOG_DIR="${SDK_ROOT}/log/screen_shot"
mkdir -p "$LOG_DIR"

FILENAME="${1:-screenshot_$(date +%Y%m%d_%H%M%S).png}"
LOCAL_PATH="${LOG_DIR}/${FILENAME}"

ADB="${SDK_ROOT}/quectel_build/tools/adb"

# 检查设备连接
if ! $ADB devices 2>/dev/null | grep -q 'device$'; then
    echo "错误: 无 ADB 设备连接"
    exit 1
fi

# 检查并安装设备端依赖
echo "检查设备端依赖..."

# ffmpeg
if ! $ADB shell "which ffmpeg" 2>/dev/null | grep -q ffmpeg; then
    echo "  安装 ffmpeg..."
    $ADB shell "apt install -y ffmpeg" 2>/dev/null
fi

# gnome-screenshot
if ! $ADB shell "which gnome-screenshot" 2>/dev/null | grep -q gnome-screenshot; then
    echo "  安装 gnome-screenshot..."
    $ADB shell "apt install -y gnome-screenshot" 2>/dev/null
fi

# 找 gnome-shell 进程 PID
SHELL_PID=$($ADB shell "pgrep gnome-shell" 2>/dev/null | head -1 | tr -d '\r')
if [ -z "$SHELL_PID" ]; then
    echo "错误: gnome-shell 未运行"
    exit 1
fi

# 从 gnome-shell 进程环境获取 D-Bus 地址和 XDG_RUNTIME_DIR
DBUS_ADDR=$($ADB shell "cat /proc/$SHELL_PID/environ" 2>/dev/null | tr '\0' '\n' | grep ^DBUS_SESSION_BUS_ADDRESS= | cut -d= -f2- | tr -d '\r')
XDG_DIR=$($ADB shell "cat /proc/$SHELL_PID/environ" 2>/dev/null | tr '\0' '\n' | grep ^XDG_RUNTIME_DIR= | cut -d= -f2- | tr -d '\r')

if [ -z "$DBUS_ADDR" ]; then
    echo "错误: 无法获取 gnome-shell D-Bus 地址"
    exit 1
fi

# 获取 gnome-shell 运行用户
SHELL_USER=$($ADB shell "ps -o user= -p $SHELL_PID" 2>/dev/null | tr -d ' \r')

echo "截图中... (user=$SHELL_USER pid=$SHELL_PID)"

# 用正确的 D-Bus 地址调用 gnome-screenshot
$ADB shell "su -s /bin/sh $SHELL_USER -c 'DBUS_SESSION_BUS_ADDRESS=$DBUS_ADDR XDG_RUNTIME_DIR=$XDG_DIR gnome-screenshot -f /tmp/gnome_shot.png' 2>/dev/null"

# 拉取到本地
$ADB pull /tmp/gnome_shot.png "$LOCAL_PATH" 2>/dev/null

if [ -f "$LOCAL_PATH" ]; then
    SIZE=$(ls -lh "$LOCAL_PATH" | awk '{print $5}')
    echo "截图已保存: ${LOCAL_PATH} (${SIZE})"
else
    echo "截图失败"
    exit 1
fi
