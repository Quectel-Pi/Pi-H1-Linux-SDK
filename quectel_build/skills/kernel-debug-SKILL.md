---
name: kernel-debug
description: "内核驱动调试：debug print 策略、一次性加够、UKI 重打包增量验证、查看方法"
version: 1.3.0
author: Hermes Agent
license: MIT
platforms: [linux]
metadata:
  hermes:
    tags: [kernel, debug, dmesg, dev_info, driver]
    related_skills: [agent-workflow, qsm565dwf-sdk]
---

# 内核驱动调试

**内核驱动编译很慢（10-30分钟），调试时必须一次性加够所有需要的 debug 信息，避免反复编译。**

## ⚠️ 核心原则：根据 adb 反馈结果分析，不要盲目猜测

**加 debug 日志之前，先想清楚要验证什么假设。加完之后，必须拿到 dmesg 输出，根据实际数据得出结论。**

### 禁止

- ❌ 看源码就下结论，不看 dmesg
- ❌ 猜测"可能是这个原因"，不去验证
- ❌ 改了一堆代码，不看输出就说"应该可以了"

### 正确流程

1. **提出假设**：比如"4K 模式被 mode_valid 拒绝了"
2. **加 debug 验证**：在 mode_valid 里打印每个模式的验证结果
3. **烧录看输出**：`adb shell dmesg | grep "[mode_valid]"`
4. **根据数据下结论**：看到 4K 模式显示 BAD → 确认是 mode_valid 的问题
5. **再提下一个假设**：比如"EDID 里根本没有4K DTD"
6. **继续加 debug 验证**... 循环直到定位根因

## 加 debug 信息的策略

### 1. 一次性加完

把所有可能需要的 debug 信息在一次编译中全部加上。包括：
- 函数入口/出口：打印参数和返回值
- 条件分支：打印走了哪个分支
- 错误路径：打印错误码和上下文
- 数据结构：打印关键字段的值

### 2. 打印 API 选择：根据变量可用性

**有 `dev` 变量时**（驱动 probe 函数、回调函数等）：

```c
dev_info(dev, "[tag] message %d\n", val);
dev_err(dev, "[tag] error: %d\n", ret);
dev_dbg(dev, "[tag] detail: %*ph\n", len, buf);  // hex dump
```

**没有 `dev` 变量时**（模块初始化、静态函数等），用更简化的 API：

```c
// pr_xxx 系列 —— 自动带模块名前缀
pr_info("[tag] message %d\n", val);
pr_err("[tag] error: %d\n", ret);
pr_debug("[tag] detail: %s\n", str);

// printk —— 最基础，任何地方都能用
printk(KERN_INFO "[tag] message %d\n", val);
printk(KERN_ERR "[tag] error %d\n", ret);

// dump_hex —— 内核自带的 hex dump（Linux 5.16+）
dump_hex("edid: ", buf, len);
```

**选择原则**：
| 场景 | 推荐 API | 原因 |
|------|----------|------|
| 有 `struct device *dev` | `dev_info/dev_err/dev_dbg` | 带设备名，方便定位 |
| 没有 `dev`，在模块代码中 | `pr_info/pr_err/pr_debug` | 带模块名前缀 |
| 任何地方，临时调试 | `printk` | 最简单，不需要额外头文件 |
| hex dump | `printk` + `%*ph` 或 `dump_hex` | 内核自带格式 |

### 3. 格式统一

用 `[%s]` 或 `[tag]` 前缀标记模块名，方便 grep 过滤：

```c
pr_info("[get_modes] EDID ver %d.%d ext=%d\n", ...);
pr_info("[mode_valid] %dx%d -> %s\n", ...);
pr_info("[bridge_mode_valid] %dx%d -> %s\n", ...);
```

## 查看调试信息

```bash
# 查看所有 debug 输出
adb shell dmesg | grep "\[模块名\]"

# 查看特定关键词
adb shell dmesg | grep -i "error\|fail\|hdmi\|dp\|drm"

# 实时跟踪
adb shell dmesg -w | grep "\[get_modes\]"
```

## 调试完成后

确认问题后，**删除或降级所有 debug print**：

- 临时调试信息：全部删除
- 有价值的长期日志：改为 `dev_dbg`（需要 `CONFIG_DRM_DEBUG` 或 `Dynamic Debug` 才输出）

```c
// 临时调试 -> 删除
// pr_info("[debug] something\n");

// 长期有价值的 -> 改为 dev_dbg
dev_dbg(dev, "[get_modes] total modes: %d\n", count);

// 没有 dev 时 -> 改为 pr_debug
pr_debug("[get_modes] total modes: %d\n", count);
```

## EDID 调试模板

```c
if (edid) {
    struct edid *e = edid;
    pr_info("[edid] ver %d.%d ext=%d\n",
            e->version, e->revision, e->extensions);
    for (int i = 0; i < 4; i++) {
        u8 *d = (u8 *)e + 0x36 + i * 18;
        if (d[0] == 0 && d[1] == 0) {
            if (d[3] == 0xFD)
                pr_info("[edid] D%d: range max_pc=%dMHz\n",
                        i, d[9] * 10);
            continue;
        }
        u16 pc = le16_to_cpup((__le16 *)d) * 10;
        u16 ha = ((d[4] >> 4) << 8) | d[2];
        u16 va = ((d[7] >> 4) << 8) | d[5];
        pr_info("[edid] D%d: %dx%d clk=%dMHz\n",
                i, ha, va, pc / 1000);
    }
}
```

## DRM 模式调试模板

```c
// dump 所有 probed modes（有 dev 时）
struct drm_display_mode *m;
int idx = 0;
list_for_each_entry(m, &connector->modes, head) {
    dev_info(dev, "[modes] mode[%d]: %dx%d @%dHz clock=%d\n",
             idx++, m->hdisplay, m->vdisplay,
             drm_mode_vrefresh(m), m->clock);
}
dev_info(dev, "[modes] total: %d\n", count);
```

## 快速验证：内核内建代码改动 → 重打 UKI 替换（Debian/UKI 启动，推荐）

**当改动的是内核内建代码（不是 .ko，比如 `drivers/usb/dwc3/core.c`、`drivers/gpu/drm/...` 里 `=y` 的部分），
不要全量 `bitbake quecpi-image` + EDL 烧录（慢、要断电进 9008），只编内核然后重打 UKI、替换板上 EFI 分区
`EFI/Linux/linux-*.efi` 即可，一轮 6~10 分钟。**

本项目（PI H1 / QCS6490 IDP / Debian）实测可用。

### 前提
- `build-qcom-wayland/` 构建目录存在，且 `ukify` 可用：
  `build-qcom-wayland/tmp-glibc/sysroots-components/x86_64/systemd-boot-native/usr/bin/ukify`
- 板子可访问（SSH 可用，`sudo` 可用；下文用 `$BOARD` 表示 `<用户>@<板子IP>`，密码按实际填）
- `WORK` 里备好 4 个输入：`linuxaa64.efi.stub`、`initrd.bin`、`cmdline`、`osrel`

### 首次准备 UKI 输入（后续复用，不用重做）

```bash
ROOT=<项目根目录>            # 例如 SDK 仓库根目录
BOARD=<用户>@<板子IP>        # SSH 登录目标
ESP=/efi                     # 板上 EFI 系统分区挂载点
PASS=<板子登录密码>          # sudo 密码同此
WORK=/tmp/ukiwork            # 主机上的 UKI 工作目录
mkdir -p $WORK && cd $WORK

# stub 来自 deploy 目录
cp $ROOT/build-qcom-wayland/tmp-glibc/deploy/images/qcm6490-idp/linuxaa64.efi.stub .

# initrd 从板上当前 UKI 里抠出来（UKI 里没有 .dtb section，DTB 走 esp 单独文件）
sshpass -p $PASS ssh $BOARD "echo $PASS | sudo -S sh -c 'objcopy -O binary -j .initrd $ESP/EFI/Linux/<UKI名>.efi /tmp/initrd.bin'"
sshpass -p $PASS scp $BOARD:/tmp/initrd.bin .

# cmdline / osrel 与当前系统保持一致（可从板上 /proc/cmdline 和 /etc/os-release 抄）
printf 'root=/dev/disk/by-partlabel/system rw rootwait console=ttynull pcie_pme=nomsi rcupdate.rcu_expedited=1 rcu_nocbs=0-7 kpti=off kasan=off kasan.stacktrace=off no-steal-acc nokaslr  swiotlb=128 mitigations=auto net.ifnames=0' > cmdline
printf 'ID=qcom-wayland\nNAME="Qualcomm Linux"\nVERSION="1.7-ver.1.1" \nVERSION_ID=1.7-ver.1.1\nPRETTY_NAME="Qualcomm Linux 1.7-ver.1.1"\nCPE_NAME="cpe:/o:openembedded:qcom-wayland:1.7-ver.1.1"\n' > osrel
```

### 每轮迭代：编译 → 打包 → 替换 → 重启

```bash
ROOT=<项目根目录>
BOARD=<用户>@<板子IP>
ESP=/efi
PASS=<板子登录密码>
WORK=/tmp/ukiwork
UKIFY=$ROOT/build-qcom-wayland/tmp-glibc/sysroots-components/x86_64/systemd-boot-native/usr/bin/ukify
IMG=$ROOT/build-qcom-wayland/tmp-glibc/work/qcm6490_idp-qcom-linux/linux-qcom-custom/6.6/build/arch/arm64/boot/Image

# 1) 只编内核（约 5-8 分钟）。用 setsid+nohup 跑，避免会话断连打断构建
cd $ROOT
nohup setsid bash -c 'cd '"$ROOT"' && source quectel_build/compile/build.sh && bitbake virtual/kernel -c compile -f; echo RC=$?' > /tmp/uk-build.log 2>&1 < /dev/null &
# 等 RC=0（不要 kill bitbake）
tail -3 /tmp/uk-build.log

# 2) 确认新 Image 生成（时间戳是刚才）
ls -la --time-style=+%H:%M:%S $IMG
# 确认改动真的进了二进制（换成你自己新加的字符串）
strings $IMG | grep -c "<你新加的字符串>"

# 3) 重打 UKI（--uname 用板上 uname -r 的输出）
cd $WORK
cp $IMG ./Image.new
python3 $UKIFY build --efi-arch=aa64 --stub=linuxaa64.efi.stub --linux=Image.new \
  --initrd=initrd.bin --cmdline=@cmdline --os-release=@osrel \
  --uname=<uname -r 输出> --output=<UKI名>.efi.new
md5sum <UKI名>.efi.new

# 4) 推到板上，校验 md5 一致后再替换（备份，可回滚）
sshpass -p $PASS scp -o StrictHostKeyChecking=no <UKI名>.efi.new $BOARD:/tmp/
sshpass -p $PASS ssh $BOARD 'md5sum /tmp/<UKI名>.efi.new'
sshpass -p $PASS ssh $BOARD "echo $PASS | sudo -S sh -c '\
  cd $ESP/EFI/Linux && \
  cp -f <UKI名>.efi <UKI名>.efi.bak && \
  mv -f /tmp/<UKI名>.efi.new <UKI名>.efi.new && sync && \
  mv -f <UKI名>.efi.new <UKI名>.efi && sync && \
  md5sum <UKI名>.efi'"

# 5) 重启（板上必须用 systemctl reboot 或 adb shell reboot，adb reboot 无效）
sshpass -p $PASS ssh $BOARD "echo $PASS | sudo -S systemctl reboot"

# 6) 等待回来，确认内核真的换了（uname -v 的编译时间戳必须变）
for i in $(seq 1 24); do sleep 5; sshpass -p $PASS ssh -o ConnectTimeout=5 $BOARD 'uname -v' 2>/dev/null && break; done
```

### 注意事项
- **UKI 里只有 `.osrel/.cmdline/.initrd`，没有 `.dtb` section**（用 `objdump -h` 确认），所以 ukify 不需要 `--devicetree`；DTB 是 esp 分区上的独立文件
- 替换后务必 `sync`，否则掉电可能起不来
- 回滚：`cp $ESP/EFI/Linux/<UKI名>.efi.bak $ESP/EFI/Linux/<UKI名>.efi && sync` 后重启
- 本项目的 `CONFIG_DYNAMIC_DEBUG` **未开启**，`dev_dbg()` 会被编译掉（`strings` 里找不到），需要现场日志时用 `dev_info()`/`pr_info()`，验证完再降级
- 校验“新代码进了二进制”：`strings Image | grep -c <你新加的字符串>`；校验“旧代码没了”：`grep -c <旧字符串>` 应为 0
- 只改 `.ko`（可卸载模块）时用下面的 .ko 替换法更快，不必动 UKI

## 快速验证：板上编译 .ko 替换模块（不需要全量烧录）

**当只修改了单个驱动 .c 文件时，可以跳过全量编译烧录，直接在主机交叉编译 .ko 推送到板子替换。**

### 前提条件
- 主机有 Yocto 交叉编译器（`aarch64-qcom-linux-gcc`）
- 内核 build 目录存在（含 Makefile、.config、Module.symvers）
- 板子上模块是可卸载的（`lsmod` 显示 refcnt=0）

### 编译步骤

```bash
# 1. 找到交叉编译器
CROSS="build-qcom-wayland/tmp-glibc/work/qcm6490-qcom-linux/firmware-qcom-bootbins/1.0/recipe-sysroot-native/usr/bin/aarch64-qcom-linux/aarch64-qcom-linux-"

# 2. 找到内核 build 目录
BUILD="build-qcom-wayland/tmp-glibc/work/qcm6490_idp-qcom-linux/linux-qcom-custom/6.6/build"

# 3. 创建临时工作目录，复制源码
WORKDIR="/tmp/lt9611_build"
rm -rf "$WORKDIR" && mkdir -p "$WORKDIR"
cp sources/quectel-src/kernel/qcom-6.6/drivers/gpu/drm/bridge/lontium-lt9611uxc.c "$WORKDIR/"

# 4. 创建 Makefile
cat > "$WORKDIR/Makefile" << 'EOF'
obj-m := lontium-lt9611uxc.o
KDIR ?= /path/to/build/dir
CROSS_COMPILE ?= /path/to/cross/compiler
ARCH ?= arm64
all:
	make -C $(KDIR) M=$(PWD) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules
clean:
	make -C $(KDIR) M=$(PWD) clean
EOF

# 5. 编译
cd "$WORKDIR" && make

# 6. 推送到板子
adb push lontium-lt9611uxc.ko /tmp/
adb shell "cp /lib/modules/\$(uname -r)/kernel/drivers/gpu/drm/bridge/lontium-lt9611uxc.ko /tmp/lontium-lt9611uxc.ko.bak"
adb shell "cp /tmp/lontium-lt9611uxc.ko /lib/modules/\$(uname -r)/kernel/drivers/gpu/drm/bridge/lontium-lt9611uxc.ko"
```

### 替换模块

```bash
# 方法一：重启设备（推荐，最安全）
adb shell reboot

# 方法二：热替换（如果模块 refcnt=0）
adb shell "rmmod lontium_lt9611uxc && modprobe lontium_lt9611uxc"

# ⚠️ 注意：如果模块被占用（refcnt != 0），rmmod 会超时/失败
# 此时必须用方法一重启设备
```

### ⚠️ 重要：adb reboot 在 QCS6490 上不管用！

**必须用 `adb shell reboot`，不能用 `adb reboot`。**

```bash
# ❌ 错误 - 在 QCS6490 上无效
adb reboot

# ✅ 正确
adb shell reboot
```

### 恢复原始模块

```bash
adb shell "cp /tmp/lontium-lt9611uxc.ko.bak /lib/modules/\$(uname -r)/kernel/drivers/gpu/drm/bridge/lontium-lt9611uxc.ko"
adb shell reboot
```

## .ko 替换验证检查清单（必须每步确认）

```bash
ADB="/path/to/quectel_build/tools/adb"
LOCAL_MD5="<编译后.md5>"
DEV_KVER=$($ADB shell "uname -r" | tr -d '\r')
MOD_PATH="/lib/modules/${DEV_KVER}/kernel/drivers/gpu/drm/bridge/<模块名>.ko"

# Step 1: 推送
$ADB push <本地.ko> /tmp/<模块名>.ko

# Step 2: 验证推送（MD5 必须一致）
$ADB shell "md5sum /tmp/<模块名>.ko"
# 对比 LOCAL_MD5，不一致则重推

# Step 3: 备份原模块
$ADB shell "cp $MOD_PATH /tmp/<模块名>.ko.orig"

# Step 4: 替换
$ADB shell "cp /tmp/<模块名>.ko $MOD_PATH"

# Step 5: 验证替换（MD5 必须一致）
FINAL_MD5=$($ADB shell "md5sum $MOD_PATH" | awk '{print $1}')
# 对比 LOCAL_MD5，不一致则排查原因

# Step 6: 重启加载新模块
$ADB shell reboot
```

### ⚠️ 注意事项
- `uname -r` 在主机上返回主机内核版本，必须用 `$ADB shell "uname -r"` 获取设备内核版本
- `/tmp` 在重启后会被清空，备份文件也会丢失，编译的 .ko 需要保留在主机上
- 每次重启后都需要重新推送 .ko 到设备
