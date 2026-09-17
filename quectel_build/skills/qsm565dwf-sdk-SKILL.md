---
name: qsm565dwf-sdk
description: "Quectel QSM565DWF (QCS6490) SDK: build firmware and flash to device."
version: 1.0.0
author: Hermes Agent
license: MIT
platforms: [linux]
metadata:
  hermes:
    tags: [qualcomm, qcs6490, qsm565dwf, quectel, sdk, build, flash, embedded]
    related_skills: [qcs6490-flash]
---

# QSM565DWF (QCS6490) SDK

## Overview

Quectel QSM565DWF 开发板的完整 SDK，支持固件编译和烧录。

- **编译**: 生成固件镜像
- **烧录**: 通过 USB EDL (9008) 模式烧录到设备

详细烧录说明请查看: `quectel_build/tools/SKILL.md`

## 编译

### 环境准备
```bash
cd <项目根目录>
source quectel_build/compile/build.sh
```

### 编译步骤
```bash
# 1. 初始化编译环境
source quectel_build/compile/build.sh

# 2. 配置编译参数（项目名、版本号、定制类型）
buildconfig QSM565DWF QSM565DWFPARL1A01_BP01.001_Linux6.6.38_V01 LINUX STD

# 3a. 全量编译（cleanall + 编译，首次或需要干净构建时使用，耗时长）
buildall

# 3b. 增量编译（只编译改动部分，推荐日常使用）
bitbake $TARGET_IMAGE

# 4. 打包固件到 buildconfig 指定的路径
buildpackage
```

**注意**:
- `buildall` = `cleanall` + 编译，会清除所有缓存重新编译，耗时很长
- 日常开发建议用 `bitbake $TARGET_IMAGE` 增量编译，只编译改动部分
- 编译后必须 `buildpackage` 才能更新固件目录

### buildconfig 参数说明（两个维度：系统 + 版本）
```
buildconfig <项目名> <版本号> <系统> <版本> [SEC]
  项目名:    QSM565DWF
  版本号:    自定义，决定固件输出目录名
  系统:      LINUX (Linux标准固件) / DEBIAN (Debian固件) / UBUNTU (Ubuntu固件)
  版本:      STD (标准版，默认 performance) / DBG (调试版，保留调试符号)
  可选标志:  SEC (安全启动)
```

**维度取值 (参考 c1.csv)**:
| 系统 | 标准版参数 (默认 perf) | Debug版参数 (固件目录名带 _DBG 后缀) |
|------|----------------------|-----------------------------------|
| Linux | LINUX STD | LINUX DBG |
| Debian | DEBIAN STD | DEBIAN DBG |
| Ubuntu | UBUNTU STD | UBUNTU DBG |

| 系统 | 固件类型 | 说明 |
|---------|---------|------|
| LINUX | Linux 标准固件 | 默认的 Yocto 标准构建 |
| DEBIAN | Debian 固件 | 基于 Debian 的 rootfs 构建 |
| UBUNTU | Ubuntu 固件 | 基于 Ubuntu 的 rootfs 构建 |

**版本说明**:
- STD: 标准/性能版 (DEBUG_BUILD=0)，固件目录名不加后缀
- DBG: 调试版 (DEBUG_BUILD=1，且不 strip 调试符号)，`buildpackage` 时固件目录名带 `_DBG` 后缀

固件目录名 = buildconfig 的第二个参数；DBG 版本在打包时追加 `_DBG`，例如：
```bash
buildconfig QSM565DWF MyCustomVersion123 LINUX STD
buildpackage
# 固件输出到: quectel_build/MyCustomVersion123/
buildconfig QSM565DWF MyCustomVersion123 LINUX DBG
buildpackage
# 固件输出到: quectel_build/MyCustomVersion123_DBG/
```

注意：系统维度（LINUX/DEBIAN/UBUNTU）**不影响**固件目录名，只影响系统形态。
同一个版本号下，DEBIAN 与 LINUX 的标准版会写入同一目录，需要区分时请使用不同的版本号
（第二个参数）。

## 烧录

### 一键烧录（推荐）
```bash
./quectel_build/tools/flash.sh
```

脚本会自动从 `quectel-buildconfig-gen.h` 读取版本号，定位固件目录。

脚本会自动：
1. 检测并安装缺少的系统依赖
2. 通过 ADB 检测设备
3. 进入 EDL (9008) 模式
4. 使用 QDL 烧录固件（保留 persist 分区）

### 手动烧录
```bash
# 进入 EDL 模式
adb shell reboot edl

# 等待 9008 设备
lsusb | grep 05c6:9008

# 执行烧录
cd quectel_build/QSM565DWFPARL1A01_BP01.001_Linux6.6.38_V01
qdl -s ufs -i . prog_firehose_Qcm6490_ddr.elf \
  partition_ufs/rawprogram[0-5].xml partition_ufs/patch[0-5].xml
```

## 项目结构

```
<项目根目录>/
├── SKILL.md                          # 本文档 (SDK 入口)
├── quectel_build/
│   ├── tools/
│   │   ├── SKILL.md                  # 烧录详细文档
│   │   ├── flash.sh                  # 一键烧录脚本
│   │   ├── adb                       # ADB 二进制
│   │   ├── qdl                       # QDL 二进制
│   │   └── 51-android.rules          # USB udev 规则
│   ├── compile/
│   │   ├── build.sh                  # 编译环境脚本
│   │   └── quectel-features-config/
│   │       └── quectel-buildconfig-gen.h  # 编译配置 (含版本号)
│   └── <版本号>/                     # 由 buildconfig 第二个参数决定
│       ├── prog_firehose_Qcm6490_ddr.elf
│       └── partition_ufs/
```

## 快速开始

### 首次使用
```bash
# 1. 初始化编译环境
source quectel_build/compile/build.sh

# 2. 配置编译参数
buildconfig QSM565DWF QSM565DWFPARL1A01_BP01.001_Linux6.6.38_V01 LINUX STD

# 3. 编译（耗时较长）
buildall

# 4. 打包固件
buildpackage

# 5. 配置 USB 权限
sudo cp quectel_build/tools/51-android.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules

# 6. 烧录到设备
./quectel_build/tools/flash.sh
```

### 日常开发
```bash
# 修改代码后增量编译（只编译改动部分）
source quectel_build/compile/build.sh
buildconfig QSM565DWF QSM565DWFPARL1A01_BP01.001_Linux6.6.38_V01 LINUX STD
bitbake $TARGET_IMAGE && buildpackage

# 烧录
./quectel_build/tools/flash.sh
```

## 注意事项

### Agent 工作指令

**当你（AI Agent）在这个项目中工作时，必须遵守以下流程：**

#### 步骤 1: 修改代码
- 直接修改源码文件
- **不要执行任何编译命令**（source、buildconfig、bitbake、buildall、buildpackage）

#### 步骤 2: 提示用户编译
修改完成后，**根据改动类型提示用户编译**：

**应用层改动**（推荐快速迭代）：
> 代码已修改完成。请在终端中执行以下命令编译单个包：
> ```bash
> cd <项目根目录>
> source quectel_build/compile/build.sh
> buildconfig QSM565DWF QSM565DWFPARL1A01_BP01.001_Linux6.6.38_V01 LINUX STD
> bitbake <包名> -c compile -f
> ```
> 编译完成后告诉我，我会通过 adb push 替换设备上的二进制文件并验证。

**内核/驱动改动**（需要完整编译）：
> 代码已修改完成（涉及内核/驱动）。请在终端中执行以下命令编译完整镜像：
> ```bash
> cd <项目根目录>
> source quectel_build/compile/build.sh
> buildconfig QSM565DWF QSM565DWFPARL1A01_BP01.001_Linux6.6.38_V01 LINUX STD
> bitbake quecpi-image && buildpackage
> ```
> 编译完成后告诉我，我会帮你烧录并验证。

#### 步骤 3: 等待用户确认
- 用户会告诉你编译打包已完成
- 确认用户看到 "Build version success" 输出

#### 步骤 4: 烧录或推送并验证
用户确认后，根据改动类型执行：

**应用层改动**（adb push）：
```bash
adb root
adb remount
adb push <编译产物路径> <设备上的路径>
adb shell sync
adb shell systemctl restart <服务名>  # 或 reboot
sleep 5
# 验证
adb shell <验证命令>
```

**内核/驱动改动**（烧录）：
```bash
./quectel_build/tools/flash.sh
sleep 30  # 等待设备启动
# 验证
adb shell getprop ro.product.model
adb shell cat /etc/os-release
```

### ⚠️ 重要：不要强制终止 Yocto 编译进程

**绝对不要用 `kill -9`、`pkill` 或类似命令强制终止 bitbake 进程！**

强制终止会导致：
- 编译状态不一致，后续编译可能失败
- sstate-cache 损坏
- 需要 `cleanall` 甚至手动清理才能恢复

正确做法：
- 等待编译自然完成
- 如果需要停止，使用 `Ctrl+C` 在终端中发送 SIGINT
- AI 代理不要操作 bitbake 进程，编译由用户手动完成

### 烧录相关
- 烧录需要 root 权限（adb shell reboot edl）
- persist 分区默认保留，不会被擦除
- QCS6490 使用 `adb shell reboot edl`（不是 `adb reboot edl`）
- 烧录中断后需要断电重新上电再进 EDL

### 编译相关
- `buildall` 会先 cleanall 再编译，耗时很长
- 日常开发用 `bitbake $TARGET_IMAGE` 增量编译
- 编译后必须 `buildpackage` 才能更新固件目录
- 建议用户在终端手动执行编译命令

### 编译策略选择

| 改动类型 | 编译方式 | 说明 |
|---------|---------|------|
| 应用层代码 | `bitbake <单个包>` + `adb push` | 快速迭代，只编译改动的包 |
| 内核/驱动改动 | `bitbake quecpi-image` + `buildpackage` | 需要重新打包完整固件 |

#### 应用层调试（推荐）
```bash
# 1. 只编译单个应用包
source quectel_build/compile/build.sh
buildconfig QSM565DWF QSM565DWFPARL1A01_BP01.001_Linux6.6.38_V01 LINUX STD
bitbake <包名> -c compile -f   # 强制重新编译

# 2. 找到编译产物
find build-qcom-wayland/tmp-glibc/work -name "<二进制文件名>" 2>/dev/null

# 3. 通过 adb push 替换到设备
adb root
adb remount
adb push <编译产物路径> <设备上的路径>
adb shell sync

# 4. 重启应用或设备验证
adb shell systemctl restart <服务名>  # 或 reboot
```

#### 内核/驱动改动
```bash
# 1. 增量编译完整镜像
source quectel_build/compile/build.sh
buildconfig QSM565DWF QSM565DWFPARL1A01_BP01.001_Linux6.6.38_V01 LINUX STD
bitbake quecpi-image

# 2. 打包到固件目录
buildpackage

# 3. 烧录到设备
./quectel_build/tools/flash.sh
```

## 设备调试

### 通用调试命令
```bash
adb shell dmesg | tail -100          # 内核日志
adb shell cat /proc/version          # 内核版本
adb shell getprop ro.build.display.id  # 系统版本
adb shell ps | grep <进程名>         # 进程状态
adb shell lsmod                      # 加载的模块
```


