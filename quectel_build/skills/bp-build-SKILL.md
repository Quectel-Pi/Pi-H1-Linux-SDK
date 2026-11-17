---
name: bp-build
description: "BP(XBL/UEFI/AOP/ADSP/CDSP/TZ) 编译：工具链版本、各子系统入口、产物汇总与打包链路、踩过的坑"
version: 1.1.0
author: Hermes Agent
license: MIT
platforms: [linux]
metadata:
  hermes:
    tags: [bp, boot, xbl, uefi, tz, aop, adsp, cdsp, llvm, hexagon]
    related_skills: [qsm565dwf-sdk, qcs6490-flash, agent-workflow]
---

# BP 编译（XBL/UEFI/TZ/AOP/ADSP/CDSP）

`BP/` 是高通 boot firmware 源码树。**不要和 Yocto 编译混在一起**：BP 走
`BP/build.sh`，Yocto 走 `quectel_build/compile/build.sh`，两套工具链互不相干。

## ⚠️ 第一条：LLVM 版本按子系统分开，不是一个版本通吃

这是最容易踩错的地方。**各子系统要求的 LLVM 版本不同，照着"全局用一个版本"一刀切会编不过**：

| 子系统 | LLVM | 依据 |
|---|---|---|
| **BOOT**（XBL/UEFI/DevPrg） | **14.0.4** | `BOOT.MXF.1.0.c1/boot_images/boot/toolchainconfig.json` 里 `CLANG140` 的 Path 硬编码 `14.0.4` |
| **TZ**（devcfg 等） | **16.0.7** | `TZ.XF.5.29.1/.../build_config_deploy_kodiak.xml` 里写的就是 16.0.7 |
| **AOP** | 16.0.7 可编 | 实测 clang 全过；但 `BP/build.sh` 的 aop 分支硬编码 `-l <14.0.4 路径>` |
| **ADSP / CDSP** | 无关 LLVM | 用 **Hexagon 8.4.07**（`HEXAGON_RTOS_RELEASE = 8.4.07`） |

**为什么 BOOT 不能用 16.0.7**（实测，别再试了）：加 `-Wno-unused-but-set-variable
-Wno-deprecated-non-prototype` 之后**所有编译都通过**，但最后合并 xbl.elf 时报

```
Exception: Segments are overlapping in xbl.elf
  Base        Size      Base+Size
  0x148ae000  0x12738   0x148c0738   ← 溢出到 0x148c0000
  0x148c0000  0x47000   0x14907000
```

clang16 生成的代码比 clang14 大，RW 段塞不进 XBL 固定地址窗口。`0x148c0000` 是
`xbl_sec` 的保留窗口，要改就得动高通 XBL 内存 map，风险极高。**结论：BOOT 用 14.0.4。**

## 工具链安装

工具链根目录用变量表示，按本机实际安装位置替换 `${TOOL_ROOT}`
（LLVM 与 Hexagon 分别装在其下，路径形如 `${TOOL_ROOT}/llvm/<版本>`
和 `${TOOL_ROOT}/HEXAGON_Tools/<版本>`）：

```bash
Q="${QSC_CLI:?高通软件中心命令行，如 qsc-cli.sh}"
TOOL_ROOT="${TOOL_ROOT:?工具链安装根目录}"

# LLVM（BOOT 要 14.0.4，TZ 要 16.0.7，两个都得装）
$Q tool download -n Snapdragon_llvm_for_arm_open_access -rv 14.0.4 --path ~/Downloads
$Q tool download -n Snapdragon_llvm_for_arm_open_access -rv 16.0.7 --path ~/Downloads
mkdir -p "$TOOL_ROOT/llvm/14.0.4"          # 属主非当前用户时先处理权限
tar xzf ~/Downloads/Snapdragon-llvm-14.0.4-linux64.tar.gz -C "$TOOL_ROOT/llvm/14.0.4"

# Hexagon 8.4.07（ADSP/CDSP 必需）
$Q tool download -n Hexagon8.4 -rv 8.4.07 --path ~/Downloads
# .qik 包要用 qik 安装；宿主缺 libicu 会直接崩，需先补依赖
"$QIK" INSTALL <file>.qik -Silent
# 装完默认落在 ~/Qualcomm/HEXAGON_Tools/<版本>，移到 $TOOL_ROOT/HEXAGON_Tools/<版本>
```

**坑**：
- `qik INSTALL` 内部会调 `sudo`，非交互 shell 下会失败；用容器 + 补 `libicu` 跑最稳。
- 工具根目录常属主为 root，普通用户建不了子目录；`sudo` 要密码时用容器挂载后 `chown`。
- 解包后检查可执行位：`find <dir>/bin -type f ! -perm -u+x` 应为空。
- LLVM 与 Hexagon 的 **版本号要和配置对得上**，不要图省事只装一个版本。

## 编译

全部在 `BP/` 目录下跑（脚本用 `pwd` 定位 SECTOOLS，不能在别处调）：

```bash
cd <SDK>/BP

# 必须导出 LLVM，否则 BOOT 报 "tool path: None doesn't exist"
# （toolchainconfig.json 里 %LLVM%bin/ 这个宏要靠它展开）
export LLVM="$TOOL_ROOT/llvm/14.0.4/"
export HEXAGON_ROOT="$TOOL_ROOT/HEXAGON_Tools"

./build.sh boot        # XBL/UEFI/DevPrg/ImageFv/SHRM...  约 2 分钟
./build.sh boot -c     # 同上但先 cleanall
./build.sh adsp        # ADSP（Hexagon 汇编，慢）        约 3.5 分钟
./build.sh cdsp        # CDSP
./build.sh tz          # TZ：实际只编 devcfg / devcfg_rfcomm（tz.mbn 是预编译）
./build.sh aop         # AOP
./build.sh             # 无参数：汇总产物到 quectel_build/
```

### 每个子系统的隐藏行为（都踩过）

- **`./build.sh aop -c` 是空操作**：脚本里只在第二参数为空时才编译，else 分支是空的。
  要 AOP 全清必须先手动删 `AOP.HO.3.6/aop_proc/core/bsp/aop/build/kodiak/`。
- **AOP 直接调 `build_kodiak.sh` 会缺 SECTOOLS**，签名阶段报
  `<...>/sectools: No such file or directory`，导致 `aop.mbn failed`。
  解决：先 export `SECTOOLS` / `SECTOOLS_DIR` 指向
  `BP/QCM6490.LE.1.0/common/sectoolsv2/ext/Linux/sectools`（及其上级目录）。
- **`./build.sh tz` 只编 devcfg**，因为 `build_config_deploy_kodiak.xml` 的 artifact
  列表只有 `devcfg` / `devcfg_rfcomm`。`tz.mbn`、`qsee.elf` 等来自基线预编译，不重编。
- **`boot -c` 会删掉 BP 里上万个 git 跟踪文件**（整个 `Build/` 目录、AOP 的 elf/mbn）。
  这些是提交进仓库的预编译产物。clean 后必须重编才能恢复；不想动就避免 `-c`。

## 产物与打包链路

`./build.sh`（无参数）会 rsync 到这些位置：

| 产物 | 去向 | 用途 |
|---|---|---|
| bootbinaries（xbl/uefi/aop.mbn/devcfg.mbn/tz.mbn...） | `quectel_build/packaged_file/bootbinaries/` | 打包进固件目录 |
| prog_firehose_*.elf | `quectel_build/packaged_file/firehose/` | 烧录工具 |
| partition_*.xml | `quectel_build/packaged_file/partition/` | 分区表 |
| QCM6490_{bootbinaries,dspso,fw}.zip | `quectel_build/prebuilt_bpfw/` | Yocto `FWZIP_PATH` |
| 各子系统 debug elf | `quectel_build/bp_elf/` | 符号解析 |

**打包不需要重编 BP**：`buildpackage` → `quectel_build/a_key_generation.sh` 会
`cp -r quectel_build/packaged_file/bootbinaries/* "$TARGET_DIR"`，把上面这些拷进
`quectel_build/<版本号>[_DBG]/`。验证方法（三方比对，比只看时间戳可靠）：

```bash
V=<版本号目录>
for f in xbl.elf uefi.elf devcfg.mbn tz.mbn aop.mbn; do
  pkg=$(md5sum "quectel_build/$V/$f" | cut -d' ' -f1)
  src=$(md5sum "quectel_build/packaged_file/bootbinaries/$f" | cut -d' ' -f1)
  base=$(git show "HEAD:quectel_build/packaged_file/bootbinaries/$f" | md5sum | cut -d' ' -f1)
  [ "$pkg" = "$src" ] && r1="固件=打包源" || r1="不一致!"
  [ "$base" != "$src" ] && r2="新编" || r2="基线旧件"
  echo "$f  $r1  $r2"
done
```

### ⚠️ 中间目录陷阱

`./build.sh` 汇总时是从 **`BP/QCM6490.LE.1.0/common/build/ufs/bin/QCM6490_bootbinaries/`**
rsync 过去的，而**这个目录也被 git 跟踪**。所以：

- 收尾时 `git checkout -- BP/` 会把中间目录**还原成基线旧件**；
- 之后再跑任意一次 `./build.sh`（哪怕没重编），就会把**基线旧件覆盖回
  `packaged_file/`**，悄悄把新产物弄丢。

对策：变更收尾时，先确认中间目录与 `packaged_file/` 的产物一致
（`md5sum` 两边对比），再决定是否 `git checkout`；或者接受
`git checkout -- BP/` 后**必须重编 + 重新汇总一次**。

### 产物 md5 不可复现的两种情况

1. **`devcfg.mbn`**：`secimage` 用 TEST 模式签名，含随机量和构建路径，同源码同工具链
   每次 md5 都不同。要验证改动必须比对**内容**：QUP 权限表在文件偏移 `0x7c88`，
   每条 20 字节 `(periph, proto, mode, uAC, allowFifo, load, modExcl)`。
2. **`xbl.elf` 等每个镜像**：内嵌构建时间戳字符串
   （`SBL1 BUILD @ <时间> on <日期>`）。两次编译 md5 不同、但字节差异只有几百字节
   且都落在该字符串附近，属正常。`cmp -l` 可定位差异，`strings` 可确认内容。

## ⚠️ SECBOOT：绝对不能随手跑

`buildpackage` 里 `SECBOOT_ENABLE=1` 时走 `a_key_generation.sh` 的 SEC 分支，
会调用签名脚本并生成 `efuse/sec.elf`。**efuse 是熔丝，烧进板子会进入 secboot
熔丝态，不可逆，板子就废了。**

- 默认 `SECBOOT_ENABLE` 未设为 1 → `is_secboot_enabled` 返回假 → 走非 SEC 分支，安全。
- 非 SEC 分支只做 rsync + cp BP 产物，与签名无关。
- 编 BP **不需要** SECBOOT_ENABLE，两件事无关。
- 跑 `buildpackage` 前建议先确认三处开关都是关的：`quectel-buildconfig-gen.h`
  有无 `SECBOOT_ENABLE` 宏、`conf/auto.conf` 有无该键、构建脚本用的临时标志文件不存在。

## 收尾

编完 `BP/` 会脏上万个文件（`__pycache__`/`LOG*`/`.sconsign`/生成的 `.mbn`/`.elf`/
`.dts`/`Docs/*.rst` 等）。提交前先看规模再决定怎么清：

```bash
cd <SDK>
git status --porcelain BP/ | awk '{print $1}' | sort | uniq -c
git status --porcelain BP/ | grep -vE '/Build/|/build/|__pycache__|\.log$|\.pyc$' | head
```

**注意**：清理 `BP/` 属于源码树操作，与已打包好的固件无关；但受上面「中间目录陷阱」
影响，清完若还要出固件，必须重编 + 重新汇总。

`BP/build.sh` 里 AOP 分支的 `-l <某 LLVM 版本路径>` 是历史遗留的辅助脚本内容，
属上游基线，不要当成"实际必须用那个版本"的证明。
