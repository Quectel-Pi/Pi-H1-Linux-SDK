---
name: ramdump-parse
description: "高通 Ramdump 解析：用 linux-ramdump-parser-v2 解析 kernel panic 后的 dump，定位崩溃根因"
version: 1.0.0
author: Hermes Agent
license: MIT
platforms: [linux]
metadata:
  hermes:
    tags: [ramdump, ramparse, panic, crash, kernel, dump, qualcomm, qcs6490]
    related_skills: [kernel-debug, qsm565dwf-sdk, qcs6490-flash]
---

# 高通 Ramdump 解析 (linux-ramdump-parser-v2)

## 背景：什么时候用这个工具

当设备发生 **kernel panic / 死机 / 看门狗超时**，且 dump 模式已开启（`AT+QCFG="dumpenable","full"`，见下方说明）时，
设备会把崩溃瞬间的 DDR 内存快照（ramdump）保存到 rawdump 分区，重启后由 `subsystem-ramdump.service` 落盘到 `/var/spool/crash`。
本工具用 **linux-ramdump-parser-v2**（高通官方开源，python3 版）配合带调试符号的 `vmlinux`，把二进制 dump 解析成可读的：
dmesg（崩溃时完整内核日志）、调度信息、各任务堆栈、看门狗状态、运行队列、中断状态等，直接定位 panic 根因。

## 工具位置

```
quectel_build/tools/
├── linux-ramdump-parser-v2/      ← 高通官方 ramdump 解析器（122 个 Python 文件）
│   ├── ramparse.py               ← 主入口
│   ├── ramdump.py                ← 核心加载逻辑（已加 --kimage-voffset 选项接入）
│   ├── local_settings.py         ← 工具链路径（由 ramparse.sh 自动生成，勿手改）
│   ├── parsers/                  ← 各子解析器(schedinfo/dmesg/watchdog/...共 65 个)
│   └── extensions/
│       ├── __init__.py
│       └── board_def.py          ← QCS6490/SC7280 板定义(本项目新增, 关键)
└── ramparse.sh                   ← 一键包装脚本（自动探测工具链+生成配置+调用）
```

排除原始 `.git` 目录，已适配 python3 + Linux 6.6 内核。**board_def.py 是本项目关键新增**：高通官方未内置 QCS6490 定义，地址从 `sc7280.dtsi`（smem@80900000）和 `qcs6490-idp.dts`（msm-id=475）提取。

## ⚠️ 前提条件

1. **dump 模式已开启**：用 `AT+QCFG="dumpenable","full"` 开启（关掉用 `"off"`），重启后通过 `/var/persist/dump_flag` 持久化。
   开启状态下写入 `/sys/module/qcom_scm/parameters/download_mode=0x700000`，panic 时触发 ramdump 落盘。
2. **发生了崩溃**：设备 panic 后会自动重启，dump 存到 rawdump 分区。
3. **有匹配的 vmlinux**（带 debug_info、未 strip）：在 Yocto build 目录里，见下方路径。vmlinux 内核版本必须与产生 dump 的固件一致，否则符号对不上。
4. **Yocto 已编译过**：提供交叉工具链（aarch64-qcom-linux-nm/objdump）。

## 快速使用（自动化）

### 1. 开启 dump 模式（一次性，仅设备从未开启时需要）

```bash
# 通过 AT 指令开启（dumpenable 为本项目的 AT 扩展）
# AT+QCFG="dumpenable","full"
# 设备会自动重启以持久化
```

确认 sysfs 已即时生效：
```bash
adb shell cat /sys/module/qcom_scm/parameters/download_mode
# full 模式应为 0x700000，off 模式应为 0x0
```

### 2. 拉取 dump 文件到本地

崩溃重启后，检查 dump 是否落盘：
```bash
adb shell ls -la /var/spool/crash/
# 应看到 *.bin / *.elf 文件，文件名含 DDR 段的 start-end 物理地址
```

把 dump 目录整体拉到本地（统一存放到项目 `log/` 目录）：
```bash
SDK_ROOT=$(git rev-parse --show-toplevel)
mkdir -p "$SDK_ROOT/log/ramdump_$(date +%Y%m%d_%H%M%S)"
DUMP_DIR="$SDK_ROOT/log/ramdump_$(date +%Y%m%d_%H%M%S)"
adb pull /var/spool/crash/. "$DUMP_DIR/"
```

### 3. 一键解析

```bash
cd <项目根目录>
./quectel_build/tools/ramparse.sh "$DUMP_DIR" "$VMLINUX"
# 脚本会自动：
#   1. 检查 python3 + pyelftools（缺则自动 pip install --user）
#   2. 在 build-qcom-wayland 下探测 aarch64-qcom-linux-nm / objdump
#   3. 生成 local_settings.py（指向探测到的 gdb/nm/objdump）
#   4. 自动找到 vmlinux（dump 目录 > build 目录，可传第二个参数显式指定）
#   5. 调用 ramparse.py --auto-dump --64-bit --force-hardware 6490 --outdir parsed_output
```

### ⚠️ KASLR dump 必须传 KIMAGE_VOFFSET（实测）

高通 QPST/Trace32 抓的整机 DDR dump **启用了 KASLR**（内核镜像被搬到高位 DDR，不在物理 0x80000000），
ramparse 自动推算 KASLR 物理基址时存在循环依赖会失败，报：
```
!!! Could not get the Linux version!
!!! Your vmlinux is probably wrong for these dumps
```
**但 DDR 里其实有 banner**——这时要手动指定 `kimage_voffset`。推算方法：

```bash
# 1. 拿 linux_banner 虚拟地址（用配套 vmlinux）
NM="quectel_build/.../aarch64-qcom-linux-nm"   # 见下方工具链路径
 "$NM" vmlinux | grep -E " D linux_banner$"
#   -> ffffffc0814f1f38 D linux_banner       (记下这个 VA)

# 2. 在 DDR dump 里找 "Linux version" 的物理地址（哪个 DDRCS*.BIN 用哪个的物理基址）
grep -abo "Linux version" DDRCS1_1.BIN   # 输出形如: 0x7c2f1f38:Linux version...
# 物理地址 = 该文件物理基址(见 dump_info.txt) + file offset
# 例: DDRCS1_1 起始 0x200000000 + 0x7c2f1f38 = 0x27c2f1f38

# 3. kimage_voffset = banner_VA - banner_phys
python3 -c "print(hex(0xffffffc0814f1f38 - 0x27c2f1f38))"
#   -> 0xffffffbe05200000

# 4. 用环境变量重跑
KIMAGE_VOFFSET=0xffffffbe05200000 ./quectel_build/tools/ramparse.sh "$DUMP_DIR" "$VMLINUX"
```
传对后 banner 会匹配成功，dmesg_TZ.txt 里能看到完整的 `Linux banner from dump = ...`。

### 4. 看结果

输出在 `DUMP_DIR/parsed_output/` 下，关键文件（实测 8GB dump 跑 `-x` 后产出 47 个文件）：

| 文件 | 内容 | 怎么看 |
|------|------|--------|
| `dmesg_TZ.txt` | 崩溃时完整内核日志 | **第一步必看**，含 panic/oops 栈、BUG 信息 |
| `tasks.txt` | 各任务内核栈（含符号） | 最大的分析文件，`grep -A20 "进程名"` |
| `tasks_highlight.txt` | 高亮任务栈 | |
| `taskdump.txt` | 任务转储 | |
| `runqueue*` | 运行队列 | 各 CPU 在跑什么 |
| `irqstate*` | 中断状态 | |
| `watchdog*` | 看门狗状态 | 区分 soft/hard lockup |
| `pstore*` | pstore 里上次 panic 记录 | |
| `vmstats.txt` / `vmalloc.txt` | 内存统计 | 怀疑内存损坏时看 |
| `thermal_info/` | 温度信息 | |
| `uevent.txt` | uevent 事件 | |
| `reserved_mem.txt` | 预留内存布局 | |
| `kconfig.txt` | 内核配置 | 确认 CONFIG 项 |
| `timerlist.txt` | 定时器列表 | |

提供给别人分析的：`dmesg_TZ.txt` + 对应任务的栈 + `parsed_output` 全目录。

## 手动调用（不用 ramparse.sh）

仅当脚本自动探测失败或要精确控制参数时：

```bash
cd quectel_build/tools/linux-ramdump-parser-v2

# 确保 local_settings.py 中的 gdb/nm/objdump 路径正确（可改可删，改用 --gdb-path 等命令行覆盖）

python3 ramparse.py \
    --auto-dump /path/to/dump_dir \
    --vmlinux /path/to/vmlinux \
    --64-bit \
    --force-hardware 6490 \
    --outdir /path/to/output_dir \
    --gdb-path /usr/bin/gdb \
    --nm-path /path/to/aarch64-qcom-linux-nm \
    -x
```

常用 ramparse 参数：

| 参数 | 作用 |
|------|------|
| `-a, --auto-dump <dir>` | 自动发现 dump 目录所有 RAM 文件 |
| `-v, --vmlinux <path>` | 带符号内核（必需） |
| `-o, --outdir <dir>` | 输出目录 |
| `--64-bit` | arm64 dump（QCS6490 默认） |
| `--force-hardware 6490` | **必须传**：QCS6490 board（避免 SMEM 自动检测失败） |
| `--kimage-voffset <val>` | **KASLR dump 必传**：见上方推算方法 |
| `-x, --everything` | 跑所有 parser（慢但全，ramparse.sh 默认带） |
| `--gdb-path` | 覆盖 gdb 路径 |
| `--nm-path` | 覆盖 nm 路径 |
| `--phys-offset <val>` | 自定义物理基址（通常不需要） |
| `--kaslr-offset <val>` | KASLR 偏移（与 --kimage-voffset 不同，通常用后者） |
| `--minidump` | 解析 minidump 格式（不是全量 DDR dump 时用） |
| `-m, --mod_path <dir>` | 指定 .ko 模块符号文件目录（解析模块内栈需要） |

## 工具链路径参考（排查自动探测失败用）

- **vmlinux（带 debug_info，未 strip）**：
  `build-qcom-wayland/tmp-glibc/work/qcm6490_idp-qcom-linux/linux-qcom-custom/6.6/build/vmlinux`
  备用：`build-qcom-wayland/tmp-glibc/deploy/images/qcm6490-idp/vmlinux`
- **aarch64 nm**：
  `build-qcom-wayland/tmp-glibc/work/qcm6490-qcom-linux/<recipe>/1.0/recipe-sysroot-native/usr/bin/aarch64-qcom-linux/aarch64-qcom-linux-nm`
- **gdb**：系统 `/usr/bin/gdb`（ramparse 会用 `gdb -ex set architecture aarch64` 切到 arm64）；
  推荐装 `gdb-multiarch`：`sudo apt install gdb-multiarch`
- **pyelftools / func_timeout**：`python3 -m pip install --user pyelftools func_timeout`

## dumpenable 开关机制（项目内 AT 扩展）

本项目的 `AT+QCFG=dumpenable` 直接写 sysfs 实现运行时即时开关，并用持久化标记：

| 模式 | sysfs 值 | 行为 |
|------|---------|------|
| full | `0x700000` | panic 时生成全量 ramdump 到 rawdump 分区 |
| off  | `0x0`     | 不生成 dump |

- AT 写 sysfs 立即生效：`/sys/module/qcom_scm/parameters/download_mode`
- 用 `/var/persist/dump_flag` 标记持久化状态
- 开机由 `pi-cleanup.sh` 读 flag 恢复 sysfs（flag 写失败不影响 AT 返回，仅降级为本次开机生效）

## 常见问题

### 解析报 "auto-parse option failed / vmlinux & DDR files manually"

dump 目录为空或 RAM 文件没被识别。确认 dump 目录非空，且文件名含物理地址段（如 `DDRCSO.RAMDUMP` 或 `md_ddr_0x80000000-0xXXXXXXXX.elf`）。

### 报 "Could not find hardware / The SMEM didn't match anything"

工具从 SMEM 区域读芯片信息失败（OCIMEM.BIN/SMEM 没解压或 SMEM 损坏）。解决：传 `--force-hardware 6490`（ramparse.sh 默认已带），直接用 `extensions/board_def.py` 里的 QCS6490 定义。若报 "bogus hardware id: 6490"，说明 board_def.py 没被加载——检查 `extensions/__init__.py` 和 `board_def.py` 是否存在。

### 报 "Could not get the Linux version / your vmlinux is probably wrong"

这是 **KASLR dump** 的典型症状：vmlinux 版本其实是对的，但内核镜像被 KASLR 搬到高位 DDR，ramparse 算错了 banner 物理地址。解决：见上方「KASLR dump 必须传 KIMAGE_VOFFSET」章节，推算 kimage_voffset 后用 `KIMAGE_VOFFSET=0x... ./ramparse.sh` 重跑。

### 栈里是十六进制地址，没有符号

vmlinux 与产生 dump 的固件内核版本不一致。确保用编译那套固件时产生的 vmlinux，版本号对得上（`nm vmlinux | grep linux_banner` 看版本字符串）。

### workqueue parser 报 TypeError: 'NoneType' object is not iterable

`parsers/workqueue.py` 的 `print_workqueue_state_3_10` 在某些 dump 上 busy_hashi 为 None。这是上游 bug，不影响整体解析——该 parser FAILED 但其它 64 个 parser 仍正常完成，dmesg/tasks 等关键输出不受影响。

### 报 No module named 'elftools'

`python3 -m pip install --user pyelftools func_timeout`。ramparse.sh 会自动装，手动跑前先装。

### 报 No module named 'local_settings'

ramparse.py 所在目录没有 `local_settings.py`。先跑一次 `ramparse.sh`（会自动生成），或手动建一个见上方"工具链路径参考"。

### gdb 报 not support aarch64 / could not load

系统 gdb 太旧。装 `gdb-multiarch`：`sudo apt install gdb-multiarch`，并让 ramparse.sh 优先用它。

## 实战技巧（dmesg parser 失败时的兜底与校验法）

以下技巧在 8GB QPST 整机 DDR dump（QCS6490 / kernel 6.6.116）上实测有效，沉淀于此供复用。

### 技巧 1：判断 "No kernel panic detected" 是否可信

ramparse 的 `CheckForPanic` 依赖 `dmesg_TZ.txt` 里的 dmesg 内容。**但 Dmesg parser 自己可能抛异常挂掉**（`dmesglib.py` 的 `extract_lockless_dmesg` 在某些 dump 上 `read_ulong` 返回 None，报 `TypeError: unsupported operand type(s) for >>: 'NoneType' and 'int'`）。

此时 `dmesg_TZ.txt` 实际只是 ramparse 自己的运行日志，不是内核 dmesg。`CheckForPanic` 在没有 dmesg 内容的情况下**必然**输出 "No kernel panic detected"——这是误导，不是真相。

**校验法**：打开 `dmesg_TZ.txt`，搜 `begin Dmesg` / `end Dmesg` 段：
- 若该段是 `--- Wrote ...` 或正常结束 → dmesg 提取成功，"No kernel panic detected" 可信。
- 若该段紧跟 `!!! Exception while running Dmesg` + Traceback → dmesg 提取失败，"No kernel panic detected" **不可信**，必须用下方技巧 2 手动恢复。

### 技巧 2：dmesg parser 失败时，手动从 DDR 提取 panic block

内核 panic 时 oops 信息会写入 printk ring buffer，落盘到 DDR。即使 ramparse 提取失败，也能用 `grep -abo` + `strings` 直接从 DDRCS*.BIN 挖出来。完整流程：

```bash
DUMP=/tmp/ramdump_test/Port_COM10   # dump 目录

# 1. 在所有 DDR 段里搜 panic 关键字符串及其字节偏移
for f in DDRCS0_0 DDRCS0_1 DDRCS1_0 DDRCS1_1; do
  echo "--- $f ---"
  grep -aboE "Kernel panic - not syncing|Oops:|Internal error:|Call trace:|pstate: |pc : |LR : |Unable to handle" \
    "$DUMP/$f.BIN" 2>/dev/null | head -10
done

# 2. 锁定 "Kernel panic - not syncing" 偏移后，提取前后 ±32KB 上下文
#    避开用户空间库（libgcrypt/Qt 等也含 "Oops:/BUG:/Internal error" 字面量，会误判）
OFF=<上一步得到的偏移，如 2004981912>
dd if="$DUMP/DDRCS0_0.BIN" bs=1 skip=$((OFF - 32768)) count=65536 2>/dev/null \
  | strings -n 5 > /tmp/oops_ctx.txt

# 3. 提取 panic 相关行（寄存器 dump / Call trace / 模块列表）
grep -nE "Oops|panic|Call trace|pstate|pc :|LR :|sp :|x[0-9]{1,2}:|Internal error|\
Fatal|SMP:|stopping|Triggering|Comm:|Tainted|Hardware name|run_timer|softirq|\
irq_exit|tick|BUG:" /tmp/oops_ctx.txt
```

**关键判别（区分真假 panic）**：
- 真 panic block 满足：紧邻 `Kernel panic - not syncing:` + `Oops:` 题头，紧跟 `CPU: x PID: y Comm: <task>` + `Hardware name: Quectel ...` + `pstate:` + `pc : 0x...` 一整块寄存器 dump + `Call trace:` 带符号。
- 假 panic（用户空间库字面量）：孤立的 `Oops:`/`Internal error`/`BUG:` 出现在 libgcrypt、Qt、GCRYPT 等字符串堆里（如 "Oops, secure memory pool already initialized"、"Internal error" 夹在 cipher.c/fips.c 路径之间），无寄存器 dump、无 Call trace，直接忽略。

### 技巧 3：用 vmlinux 符号表反查 Call trace 的具体偏移行

ramparse 有时会漏提 Call trace 的尾部符号（或符号不全）。用 nm + 源码反查：

```bash
NM="<aarch64-qcom-linux-nm 路径>"   # 见上方「工具链路径参考」
VMLINUX="<固件 vmlinux 路径>"

# 1. 拿函数基地址
"$NM" "$VMLINUX" | grep -E " t| T arm_smmu_global_fault$"
# -> ffffffc0809ef8a4 t arm_smmu_global_fault

# 2. Call trace 报 arm_smmu_global_fault+0x2e8/0x45c
#    崩溃 VA = 基地址 + 0x2e8 = ffffffc0809ef8a4 + 0x2e8 = ffffffc0809efb8c
python3 -c "print(hex(0xffffffc0809ef8a4 + 0x2e8))"

# 3. 找该函数源码位置（用 addr2line，或在源码里搜函数体）
aarch64-qcom-linux-addr2line -e "$VMLINUX" -f ffffffc0809efb8c
```

本例 +0x2e8 落在 `arm_smmu_rpm_get` → `pm_runtime_resume_and_get` 的调用点，印证 Call trace 第二段 `rpm_resume → schedule()` 在 atomic 上下文调度的根因。

### 技巧 4：KASLR kimage_voffset 推算校验法（三连一致）

skill 上文已给推算公式。补充**校验**：推算对错不要只看 dmesg_TZ 开头不报 "Could not get the Linux version"，要搜三行 banner 必须字面一致：

```
grep -E "Linux (Banner|banner)" parsed_full/dmesg_TZ.txt
```

应看到三行完全一致的 banner（vmlinux 的 / dump 的 / 初始的），任何一行为空或不匹配都说明 voffset 错。本例正确值 `0xffffffbe05200000` 三行全中；错误的 `--kaslr-offset 0xffffffbebb200000`（parsed_test1）和强改 `--phys-offset`（parsed_test2）都因 banner 不匹配在第一关就退出。

### 技巧 5：解析输出目录命名约定（避免重蹈多目录乱象）

一次 dump 解析应在**一个**输出目录完成。失败重试请删旧目录或换名，不要用 parsed_test1/test2/output/kaslr/full 这种平行目录——ramparse 不覆盖同名输出，且多目录会让人混淆哪个是成功那份。推荐：

```bash
# 始终用 parsed_output；重跑前清空
rm -rf parsed_output
KIMAGE_VOFFSET=0xffffffbe05200000 ./quectel_build/tools/ramparse.sh "$DUMP" "$VMLINUX"
```

### 技巧 6：QPST 整机 dump 的复位原因核查

QPST dump 的 `dump_info.txt` 末尾常含 `RST_STAT.BIN`（reset status, 物理 0x80791198, 4 字节）。读了它可区分：看门狗咬 / panic 重启 / 冷启动 / EDL。但 QPST 抓的整机 dump `RST_STAT.BIN` 通常没单独落盘，只能从 `DDRCS0_0.BIN` 对应偏移取 4 字节（或者直接看 panic block 里是否含 "Causing a QCOM Apps Watchdog bite!"——有则次生于看门狗，无则是真 panic）。

本例 panic block 末尾出现的 `gh-watchdog: Causing a QCOM Apps Watchdog bite!` 和 `msm_dpu hangcheck detected gpu lockup` 都是 **CPU1 panic 卡死后唤醒的次生现象**，不是根因。

### 技巧 7：⚠️ 复位类 dump 必先挖 PBL/SBL1 启动日志（比看门狗现象根本得多）

**这是最重要的一条：遇到复位/死机 dump，不要停在内核现场（看门狗 bite / scandump / swapper 状态 R）就下结论——那些几乎全是二级现象。真正的复位根因往往在 bootloader 残留日志里，尤其是 PMIC 电源故障。**

高通 SBL1/PBL 在启动时会把结构化日志（`Log Type - Time(microsec) - Message` 格式）写到 DDR，崩溃重启后这段日志**残留在 `DDRCS0_0.BIN` 里**（实测本项目 QCS6490 在偏移 `0x723e00`~`0x726200` 附近，不同 dump 偏移会变，用下面的方法搜）。这里直接打印复位原因链和 PMIC OCP 详情，信息量远大于 RST_STAT 单字节。

```bash
DUMP=/path/to/Port_COMxx

# 1. 定位 SBL1 日志区（搜启动日志特征串）
grep -aboE "SBL1, Start|SBL1 BUILD|Format: Log Type|PM: Reset by|OCP Occured|QC_IMAGE_VERSION_STRING" \
  "$DUMP/DDRCS0_0.BIN" | head

# 2. 命中后 dump 该区域（起点往前留几 KB），提取可读日志
OFF=<上一步 SBL1/Format 命中的偏移>
dd if="$DUMP/DDRCS0_0.BIN" bs=1 skip=$((OFF - 4096)) count=20000 2>/dev/null | strings -n 3
```

**重点看这几类行（复位原因，从最底层电源往上看）：**

| 日志行 | 含义 |
|--------|------|
| `PM: Reset by AMBERJACK OCP` | **PMIC 过流保护（OCP）触发的硬件复位——根因级** |
| `PM: OCP-ed_PMIC: 0xN` | 哪颗 PMIC 发生 OCP |
| `PM: OCP Occured: PMIC: X; LDO: Y` | **具体是哪颗 PMIC 的第 Y 路 LDO 稳压器过流**（据此查原理图/DTS 锁定该电源轨上的短路/异常拉流外设） |
| `PM: Reset by PSHOLD` / `Warm Reset` | 软件触发的热复位（可能是 OCP 后的连锁重试） |
| `PM: Warm reset count:0xN` | 热复位计数，**count 越小越早**——原因链要按 count 从小到大读（0x1 是最初那次） |
| `PM: Reset by <PON reason>` | 各种 PON（Power-On）复位原因 |

**判读原则（血泪教训）：**
- **复位原因链从最底层（PMIC/PBL）往上看，不要从内核现场往下猜。** PMIC 的 OCP 是硬件级、最先发生的事件，它一断电，内核那边就冻结出 scandump / 看门狗 bite / swapper 卡 R 等一堆"死状"，但那些都是果不是因。
- 看到 `Reset by ... OCP` + `OCP Occured: PMIC:x LDO:y`，根因就是**那条 LDO 供电轨过流**，下一步去 DTS/原理图查 LDO-y 供的是哪个外设，锁定短路/异常拉流源头。
- 本项目实测：一份被误判为"CPU0 卡死→Apps Watchdog bite 硬复位"的 dump，实际根因是 SBL1 日志里的 `Reset by AMBERJACK OCP` + `OCP Occured: PMIC:1 LDO:17`——PMIC 主片 LDO17 过流。scandump/看门狗全是二级现象。**只看内核层会得出错误结论。**

### 技巧 8：⚠️ dmesg parser 崩溃根因——宿主 gdb 不支持 aarch64 导致 sizeof 错乱（已修复）

**现象**：`dmesg_TZ.txt` 里 `begin Dmesg` 紧跟：
```
!!! Exception while running Dmesg
  File ".../dmesglib.py", line 210, in extract_lockless_dmesg
    state = 3 & (self.ramdump.read_ulong(descs_addr + desc_off + sv_off) >> desc_flags_shift)
TypeError: unsupported operand type(s) for >>: 'NoneType' and 'int'
```
（技巧 1 已说过此时 "No kernel panic detected" 不可信。本文档补充**为什么**会崩。）

**根因（血泪排查记录）**：宿主 gdb（如 Ubuntu 默认 `gdb`）**不支持 aarch64 架构**（`gdb -batch -ex "set architecture aarch64"` 报 `Undefined item: "aarch64"`），但它仍能用默认类型系统载入 arm64 vmlinux，导致：
```
gdb: p sizeof(void*) = 4        ← 错! arm64 应为 8
gdb: p sizeof(unsigned long) = 4  ← 错!
gdb: p sizeof(struct printk_ringbuffer*) = 8  ← 具体指针类型反而对
```
而 `ramdump.read_pointer()` 用 `sizeof('void *')` 决定读 32 位还是 64 位（`ramdump.py` 2575 行）→ **把 prb 指针当成 32 位读，丢掉高 32 位**（`0xffffffc0821ceea8` 读成 `0x821ceea8`）→ `virt_to_phys` 映射到错误物理地址 → 读到任务名垃圾 → desc_ring 字段全错 → `read_ulong` 返回 None → TypeError。

**排查确认方法**（在已加载 dump 的 python 环境里）：
```python
dump.arm64                    # True
dump.sizeof('void *')         # 4 ← 异常! arm64 应为 8
dump.read_pointer('prb')      # 0x821ceea8 ← 丢高32位!
dump.read_u64('prb')          # 0xffffffc0821ceea8 ← 正确
```

**修复（已合入本项目 ramdump.py）**：`RamDump.sizeof()` 加 arm64 兜底——当 `sz==4 and self.arm64` 且类型是 `void * / unsigned long / long / unsigned long long / long long` 时强制返回 8。一处修改，ramdump.py + dmesglib.py + 所有用 `sizeof()` 算指针步长的 parser（ftrace/lockdep/slabinfo/zram 等）全部受益。

**验证**：修复后 `read_pointer('prb')` 返回完整 64 位，`extract_lockless_dmesg` 成功提取 2249 行 dmesg（含 `[uptime][TID]` 前缀，与手工挖 `__log_buf` 的内容一致）。

**根治建议**：`sudo apt install gdb-multiarch`（gdb 正确识别 arm64，`sizeof(void*)=8` 天然正确），ramparse.sh 已优先探测 gdb-multiarch。若装不了，靠 ramdump.py 的兜底也够。

## 日志存放约定

所有 dump 和解析结果统一放到项目 `log/` 目录：
```
<SDK根目录>/log/
└── ramdump_YYYYMMDD_HHMMSS/
    ├── *.elf / *.bin         ← 原始 dump（adb pull 来的）
    └── parsed_output/        ← ramparse 解析结果
        ├── dmesg_TZ.txt
        ├── *_task*.txt
        └── ...
```
