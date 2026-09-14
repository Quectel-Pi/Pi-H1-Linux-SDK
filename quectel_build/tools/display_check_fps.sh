#!/bin/bash

# 多接口显示帧率监测脚本
# 同时监测所有 intf 的帧率

INTERVAL=5  # 采样间隔（秒）

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
PURPLE='\033[0;35m'
NC='\033[0m' # No Color

# 显示脚本信息
show_header() {
    clear 2>/dev/null || echo -e "\n"
    echo -e "${BLUE}========================================${NC}"
    echo -e "${GREEN}多接口显示帧率监测工具${NC}"
    echo -e "${BLUE}========================================${NC}"
    echo -e "采样间隔: ${YELLOW}$INTERVAL 秒${NC}"
    echo -e "监测所有: ${CYAN}intf:0, intf:1, ...${NC}"
    echo -e "${BLUE}========================================${NC}"
    echo ""
}

# 检查权限和文件 - 修正版
check_prerequisites() {
    # 使用 ls 检查通配符路径是否存在文件
    if ! ls /sys/kernel/debug/dri/0/encoder*/status >/dev/null 2>&1; then
        echo -e "${RED}错误: 无法访问 /sys/kernel/debug/dri/0/encoder*/status${NC}"
        echo "请确保以 root 权限运行，且内核调试文件系统已挂载"
        exit 1
    fi
}

# 获取所有接口的当前状态
get_all_interfaces_status() {
    cat /sys/kernel/debug/dri/0/encoder*/status 2>/dev/null | \
        grep "intf:" | \
        awk '{intf=""; vsync=""; underrun=""; mode="";
              for(i=1;i<=NF;i++){
                if($i~/^intf:/) intf=$i;
                else if($i~/^vsync:/) vsync=$(i+1);
                else if($i~/^underrun:/) underrun=$(i+1);
                else if($i~/^mode:/) mode=$(i+1);
              }
              gsub(":", "", intf);
              print intf":"vsync":"underrun":"mode}'
}

# 解析状态行
parse_status() {
    local line=$1
    IFS=':' read -r intf vsync underrun mode <<< "$line"
    echo "$intf $vsync $underrun $mode"
}

# 主函数
main() {
    show_header
    check_prerequisites
    
    echo -e "${YELLOW}正在初始化监测...${NC}"
    echo ""
    
    # 用于存储前后两次的vsync值
    declare -A PREV_VSYNC
    declare -A PREV_UNDERRUN
    declare -A PREV_MODE
    
    # 用于存储时间戳
    PREV_TIMESTAMP=$(date +%s.%N)
    
    # 首次采样
    while read line; do
        if [ -n "$line" ]; then
            read intf vsync underrun mode <<< $(parse_status "$line")
            PREV_VSYNC[$intf]=$vsync
            PREV_UNDERRUN[$intf]=$underrun
            PREV_MODE[$intf]=$mode
        fi
    done < <(get_all_interfaces_status)
    
    # 如果没有检测到任何接口
    if [ ${#PREV_VSYNC[@]} -eq 0 ]; then
        echo -e "${RED}错误: 未检测到任何接口${NC}"
        echo "尝试直接查看:"
        ls -la /sys/kernel/debug/dri/0/encoder*/status 2>/dev/null || echo "无encoder文件"
        exit 1
    fi
    
    echo -e "检测到 ${GREEN}${#PREV_VSYNC[@]}${NC} 个显示接口"
    echo ""
    
    # 主循环
    while true; do
        sleep $INTERVAL
        
        # 当前采样
        declare -A CURR_VSYNC
        declare -A CURR_UNDERRUN
        declare -A CURR_MODE
        
        while read line; do
            if [ -n "$line" ]; then
                read intf vsync underrun mode <<< $(parse_status "$line")
                CURR_VSYNC[$intf]=$vsync
                CURR_UNDERRUN[$intf]=$underrun
                CURR_MODE[$intf]=$mode
            fi
        done < <(get_all_interfaces_status)
        
        # 计算时间差
        CURR_TIMESTAMP=$(date +%s.%N)
        TIME_DIFF=$(echo "$CURR_TIMESTAMP - $PREV_TIMESTAMP" | bc 2>/dev/null || echo "$INTERVAL")
        
        # 清屏或换行
        if [ $INTERVAL -ge 2 ]; then
            clear 2>/dev/null || echo -e "\n\n"
            show_header
        else
            echo ""
        fi
        
        # 显示当前时间
        echo -e "${CYAN}时间: $(date '+%Y-%m-%d %H:%M:%S')${NC}"
        printf "${CYAN}采样间隔: %.3f秒${NC}\n" $TIME_DIFF
        echo ""
        
        # 表格头部
        printf "${BLUE}%-8s | %-10s | %-10s | %-8s | %-10s | %-8s${NC}\n" \
               "接口" "当前vsync" "增长" "帧率(fps)" "underrun" "模式"
        printf "${BLUE}%-8s-|-%-10s-|-%-10s-|-%-8s-|-%-10s-|-%-8s${NC}\n" \
               "--------" "----------" "----------" "--------" "----------" "--------"
        
        # 检测活跃接口
        ACTIVE_INTERFACES=""
        
        # 遍历所有接口并显示
        for intf in $(echo "${!CURR_VSYNC[@]}" | tr ' ' '\n' | sort); do
            curr_vsync=${CURR_VSYNC[$intf]}
            prev_vsync=${PREV_VSYNC[$intf]:-0}
            curr_underrun=${CURR_UNDERRUN[$intf]}
            curr_mode=${CURR_MODE[$intf]}
            
            # 计算增长和帧率
            vsync_diff=$((curr_vsync - prev_vsync))
            
            if [ $vsync_diff -gt 0 ] && [ $(echo "$TIME_DIFF > 0" | bc 2>/dev/null || echo "1") -eq 1 ]; then
                fps=$(echo "scale=2; $vsync_diff / $TIME_DIFF" | bc 2>/dev/null || echo "0")
                # 标记为活跃接口
                ACTIVE_INTERFACES="$ACTIVE_INTERFACES $intf(${fps}fps)"
            else
                fps="0.00"
            fi
            
            # 根据活跃程度选择颜色
            if [ $vsync_diff -gt 0 ]; then
                # 活跃接口 - 绿色
                printf "${GREEN}%-8s | %-10s | +%-9s | %-8s | %-10s | %-8s${NC}\n" \
                       "$intf" "$curr_vsync" "$vsync_diff" "$fps" "$curr_underrun" "$curr_mode"
            else
                # 非活跃接口 - 黄色
                printf "${YELLOW}%-8s | %-10s | %-10s | %-8s | %-10s | %-8s${NC}\n" \
                       "$intf" "$curr_vsync" "-" "-" "$curr_underrun" "$curr_mode"
            fi
        done
        
        echo ""
        
        # 显示活跃接口摘要
        if [ -n "$ACTIVE_INTERFACES" ]; then
            echo -e "${GREEN}▶ 当前活跃接口:${NC}$ACTIVE_INTERFACES"
        else
            echo -e "${YELLOW}▶ 当前没有活跃接口 (所有vsync无增长)${NC}"
        fi
        
        # 显示提示
        echo ""
        echo -e "${CYAN}提示: vsync持续增长的接口即为正在输出的显示接口${NC}"
        
        # 更新前一帧数据
        for intf in "${!CURR_VSYNC[@]}"; do
            PREV_VSYNC[$intf]=${CURR_VSYNC[$intf]}
            PREV_UNDERRUN[$intf]=${CURR_UNDERRUN[$intf]}
            PREV_MODE[$intf]=${CURR_MODE[$intf]}
        done
        PREV_TIMESTAMP=$CURR_TIMESTAMP
    done
}

# 捕获 Ctrl+C 信号
cleanup() {
    echo -e "\n${GREEN}监测已停止${NC}"
    exit 0
}
trap cleanup SIGINT

# 运行主函数
main