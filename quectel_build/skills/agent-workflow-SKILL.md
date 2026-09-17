---
name: agent-workflow
description: "Quectel SDK Agent 工作流程: 修改代码、提示编译、烧录验证"
version: 1.1.0
author: Hermes Agent
license: MIT
platforms: [linux]
metadata:
  hermes:
    tags: [qualcomm, qcs6490, quectel, workflow, agent]
    related_skills: [qsm565dwf-sdk, qcs6490-flash]
---

# Agent 工作流程

**修改代码后必须遵守以下流程：**

## 1. 修改代码
直接修改源码，不要执行编译命令。

## 2. 提示用户编译
修改完成后，提示用户手动编译：

**应用层改动**（如 AT 指令、应用服务）：
> 请在终端执行：
> ```bash
> cd <项目根目录>
> source quectel_build/compile/build.sh
> buildconfig QSM565DWF QSM565DWFPARL1A01_BP01.001_Linux6.6.38_V01 <系统> <版本>
> bitbake <包名> -c compile -f
> ```
> 编译完成后告诉我，我会通过 adb push 替换验证。

**内核/驱动改动**：
> 请在终端执行：
> ```bash
> cd <项目根目录>
> source quectel_build/compile/build.sh
> buildconfig QSM565DWF QSM565DWFPARL1A01_BP01.001_Linux6.6.38_V01 <系统> <版本>
> bitbake quecpi-image && buildpackage
> ```
> 编译完成后告诉我，我会烧录并验证。

**定制类型说明（两个维度：系统 + 版本）**:
```
buildconfig <项目名> <版本号> <系统> <版本> [SEC]
  系统:  LINUX (Linux标准固件) / DEBIAN (Debian固件) / UBUNTU (Ubuntu固件)
  版本:  STD (标准版，默认 perf) / DBG (调试版，固件目录名带 _DBG 后缀，保留调试符号)
```

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

请根据用户需要的固件类型（系统 + 版本）选择对应的两个维度参数。

## 3. 等待用户确认
用户确认编译打包完成后，执行下一步。

## 4. 烧录或推送并验证
- **应用层**：adb push 替换二进制，重启服务验证
- **内核层**：执行 `./quectel_build/tools/flash.sh` 烧录，重启设备验证

## 重要警告

- **不要用 kill/pkill 终止 bitbake 进程**，会导致编译环境损坏
- **不要执行 source/buildconfig/bitbake/buildall/buildpackage**，编译由用户手动完成
- QCS6490 进入 EDL 用 `adb shell reboot edl`（不是 `adb reboot edl`）
- **⚠️ QCS6490 重启必须用 `adb shell reboot`，`adb reboot` 不管用！**
- **内核驱动调试快捷方式**：修改单个 .c 文件时，可用交叉编译器编译 .ko 推送替换，不需要全量烧录（详见 kernel-debug-SKILL.md）

## 内核驱动调试原则

**内核驱动编译很慢，每次编译可能需要10-30分钟。调试时必须一次性加够所有需要的 debug 信息，避免反复编译。**

### ⚠️ 核心原则：根据 adb 反馈结果分析，不要盲目猜测

**加 debug 日志之前，先想清楚要验证什么假设。加完之后，必须拿到 dmesg 输出，根据实际数据得出结论。**

**禁止**：❌ 看源码就下结论，不看 dmesg；❌ 猜测原因不去验证；❌ 改了代码不看输出就说"应该可以了"

**正确流程**：提出假设 → 加 debug 验证 → 烧录看 dmesg → 根据数据下结论 → 循环直到定位根因

### 加 debug 信息的策略

1. **一次性加完**：把所有可能需要的 debug 信息在一次编译中全部加上
2. **用 dev_info/dev_dbg**：内核驱动用 `dev_info` 或 `dev_dbg` 输出，通过 `dmesg` 查看
3. **关键路径全覆盖**：
   - 函数入口/出口：打印参数和返回值
   - 条件分支：打印走了哪个分支
   - 错误路径：打印错误码和上下文
   - 数据结构：打印关键字段的值
4. **格式统一**：用 `[%s]` 前缀标记模块名，方便 grep 过滤

### 示例

```c
// 函数入口
dev_info(dev, "[%s] enter: mode=%dx%d@%dHz\n", __func__,
         mode->hdisplay, mode->vdisplay, drm_mode_vrefresh(mode));

// 条件分支
dev_info(dev, "[%s] branch: %s\n", __func__,
         (condition) ? "taken" : "not taken");

// 返回值
dev_info(dev, "[%s] exit: ret=%d\n", __func__, ret);

// EDID 调试
dev_info(dev, "EDID block %d: %*ph\n", block, len, buf);
```

### 查看调试信息

```bash
adb shell dmesg | grep -i "模块名\|关键词"
```

### 调试完成后

确认问题后，**删除或降级所有 debug print**：
- 临时调试信息：全部删除
- 有价值的长期日志：改为 `dev_dbg`（需要 `CONFIG_DRM_DEBUG` 才输出）

## 烧录失败处理

烧录过程中可能遇到以下问题，需要提示用户自行处理：

### 1. USB 接口占用 (qdl: failed to claim USB interface)
**现象**: `qdl: failed to claim USB interface`，重试多次仍然失败
**原因**: USB 设备状态异常，可能被其他进程占用或设备未正确进入 EDL
**处理**: 
1. 提示用户断电板子
2. 重新上电
3. 确认进入 EDL 模式后重新烧录
**禁止**: 不要修改 flash.sh 脚本，不要尝试 kill 进程

### 2. 设备未进入 EDL 模式
**现象**: 等待 9008 设备超时
**处理**:
1. 提示用户检查 USB 连接
2. 手动执行 `adb shell reboot edl`
3. 或者断电重新上电

### 3. 烧录中断
**现象**: 烧录过程中连接断开
**处理**:
1. 断电板子
2. 重新上电进入 EDL
3. 重新烧录
