# QCS6490 一键烧录

## Overview
通过 USB EDL (Emergency Download) 模式烧录 QCS6490 (Quectel QSM565DW) 固件。
使用 Qualcomm QDL 工具 + Sahara/Firehose 协议，一条命令完成全部烧录。

## 前置条件

### 硬件
- QCS6490 开发板 (Quectel QSM565DW)
- USB 数据线连接到 PC
- 板子需要有 root 权限（adb shell reboot edl 需要）

### 软件依赖
脚本会自动检测并安装缺少的依赖（需要 sudo 权限）：
- libusb-1.0-0 (USB 通信)
- libxml2-dev (XML 解析)
- libzip-dev (压缩支持)
- usbutils (lsusb 命令)

手动安装：
```bash
sudo apt install -y libusb-1.0-0 libxml2-dev libzip-dev usbutils
```

tools/ 目录已包含预编译的 adb 和 qdl 二进制（x86-64），无需额外安装。

### udev 规则（首次使用）
```bash
sudo cp quectel_build/tools/51-android.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger
```

## 文件结构
```
quectel_build/
├── tools/
│   ├── flash.sh              # 一键烧录脚本
│   ├── adb                    # ADB 二进制 (x86-64)
│   ├── qdl                    # QDL 二进制 (x86-64)
│   ├── 51-android.rules       # USB udev 规则
│   └── flash-SKILL.md         # 本文档
├── compile/quectel-features-config/
│   └── quectel-buildconfig-gen.h  # 固件版本配置
└── <固件目录>/                     # 从配置文件自动读取
    ├── prog_firehose_Qcm6490_ddr.elf
    └── partition_ufs/
```

固件目录名从 `quectel-buildconfig-gen.h` 中的 `QUECTEL_PROJECT_REV` 自动读取，无需硬编码。

## 使用方法
### 一键烧录（推荐）
```bash
cd <项目根目录>
./quectel_build/tools/flash.sh
```

脚本会自动检测并安装缺少的系统依赖（需要 sudo 权限）。
默认使用 UFS 存储。如果是 eMMC：
```bash
./quectel_build/tools/flash.sh emmc
```

### 脚本流程
1. 检查 ADB 设备连接
2. 执行 `adb shell reboot edl` 进入 9008 EDL 模式
3. 等待 Qualcomm USB 9008 设备出现（最多 30 秒）
4. 调用 qdl 烧录固件（自动排除 WIPE/BLANK_GPT，保留 persist 分区）
5. 设备自动重启

### 手动分步操作
如果脚本有问题，可以手动执行：
```bash
# 1. 进入 EDL 模式
adb shell reboot edl

# 2. 等待 9008 设备
lsusb | grep 05c6:9008

# 3. 执行烧录
cd quectel_build/QSM565DWFPARL1A01_BP01.001_Linux6.6.38_V01
qdl -s ufs -i . \
  prog_firehose_Qcm6490_ddr.elf \
  partition_ufs/rawprogram[0-5].xml \
  partition_ufs/patch[0-5].xml
```

## 关键参数

| 参数 | 说明 |
|------|------|
| `-s ufs` | 存储类型 (ufs/emmc) |
| `-i <dir>` | 固件文件搜索目录 |
| `-d` | 调试模式（详细输出） |
| `-n` | Dry run（不实际烧录） |
| `-f` | 允许跳过缺失文件 |
| `-R` | 烧录后不重启 |

## 注意事项

### EDL 进入方式
QCS6490 必须用 `adb shell reboot edl`，`adb reboot edl` 不可用。
需要 adb 连接且有 root 权限。

### persist 分区
脚本默认排除 `*_WIPE_PARTITIONS.xml` 和 `*_BLANK_GPT.xml`，
不会擦除 persist 分区。如需全量擦除，手动指定 WIPE 文件。

### 烧录中断恢复
如果烧录中断导致设备卡在异常 Firehose 状态：
1. 断电 → 上电 → 重新进入 EDL
2. 不要直接重试 qdl，必须先断电让设备重新枚举

### 已知问题
- 烧录过程中输出可能被缓冲，看不到实时进度
- 首次烧录约需 30-60 秒（取决于固件大小）

## 验证烧录成功
```bash
# 烧录后等待设备启动
adb devices   # 应显示设备序列号
adb shell getprop ro.product.model   # 应返回 QCS6490 相关型号
```

## 故障排除

| 问题 | 解决方案 |
|------|----------|
| adb devices 为空 | 检查 USB 线、开启 USB 调试 |
| 等待 9008 超时 | 检查 udev 规则、lsusb 查看设备 |
| firehose detect 失败 | 断电重新上电，再进 EDL |
| Read non multiple sector size | 设备 Firehose 状态异常，断电重试 |
| 权限不足 | 使用 sudo 或配置 udev 规则 |
