# 组件迭代：改了 Yocto 某个包，怎么发到设备的 apt 源

面向接手这个 SDK 的其他 agent。前提：`deb-track` 已工作（见
`.pi/skills/deb-track/SKILL.md`）。本 skill 只讲**改完之后怎么让设备升上去**。

## 一句话前提

**改了代码但版本号没变 = 迭代不生效。** apt 只比版本号，不比文件内容。

## 三个独立的"版本"，别混

这是本项目最容易搞错的地方，必须分清：

| 概念 | 当前值 | 定义位置 | 影响什么 |
|---|---|---|---|
| **包版本** | `6.6-r0` | recipe 的 `PR`（默认 `r0`） | **决定 apt 是否升级** |
| **内核版本串** | `6.6.116-qli-1.7-ver.1.1` | `CONFIG_LOCALVERSION` | **决定包名**（`kernel-module-xxx-<串>`） |
| **固件版本** | `1.7-ver.1.1` | `DISTRO_VERSION` | 固件目录名，间接决定上面那个串 |

内核版本串的完整生成链（实测确认）：

```
layers/meta-qcom-distro/conf/distro/include/qcom-base.inc:3
    DISTRO_VERSION = "1.7-ver.1.1"
qcom-base.inc:6
    LINUX_VERSION_EXTENSION = "-qli-${DISTRO_VERSION}"        → "-qli-1.7-ver.1.1"
layers/meta-qcom-hwe/recipes-kernel/linux/linux-qcom-custom_6.6.bb:85
    echo 'CONFIG_LOCALVERSION="${LINUX_VERSION_EXTENSION}"' >> ${B}/.config
内核 .config
    CONFIG_LOCALVERSION="-qli-1.7-ver.1.1"
KERNEL_VERSION  = 6.6.116 + CONFIG_LOCALVERSION
包名            = kernel-module-btqca-6.6.116-qli-1.7-ver.1.1
```

## 三类组件，三种迭代方式

### A. 独立 BSP recipe（如 msm-va-driver）

包名不含内核版本，**最简单**。

```bash
# 1. 改代码：bump recipe 的 SRCREV（或改 files/ 里的源码）
#    layers/meta-quectel/recipes-multimedia/msm-va-driver/msm-va-driver_1.0.bb
#      SRCREV = "<新的 commit>"
#    或者只改 files/src/*.c（recipe 从 files/ 构建）

# 2. 递增包版本 —— 在同一个 recipe 里加：
#      PR = "r1"          # 原来是默认 r0 → 变成 1.0-r1
#    （或者改 PV，如 PV = "1.1"）

# 3. 只重建这个 recipe
bitbake msm-va-driver -c package_write_deb -f

# 4. 确认新版本
ls build-qcom-wayland/tmp-glibc/deploy/deb/*/msm-va-driver_*.deb
dpkg-deb -f <新的> Version        # 必须是 1.0-r1

# 5. 发布（不用重建镜像！）
./quectel_build/tools/deb-publish.sh --from-lists
```

### B. 内核模块（684 个 `kernel-module-*`）

分两种情况，**差别巨大**：

#### B1. 只改驱动代码，不动内核版本串（常见）

```bash
# 1. 改内核源码：bump SRCREV
#      layers/meta-qcom-hwe/recipes-kernel/linux/linux-qcom-custom_6.6.bb
#        SRCREV = "<新的 commit>"
#    或者只改本仓库的 patch（0001-QCLINUX-*.patch）

# 2. 递增包版本 —— 在 linux-qcom-custom_6.6.bb 里加：
#      PR = "r1"
#    注意：这一改，**684 个 kernel-module 包 + kernel-image 全变成 6.6-r1**

# 3. 重建内核的 deb
bitbake virtual/kernel -c package_write_deb -f

# 4. 发布
./quectel_build/tools/deb-publish.sh --from-lists
```

包名不变（还是 `kernel-module-btqca-6.6.116-qli-1.7-ver.1.1`），只有版本
`6.6-r0 → 6.6-r1`，所以是**正常升级**，旧 .ko 会被原地覆盖。

#### B2. 改了 `DISTRO_VERSION`（或 `CONFIG_LOCALVERSION`）

**这是陷阱。** 包名会变：

```
kernel-module-btqca-6.6.116-qli-1.7-ver.1.1     ← 旧包
kernel-module-btqca-6.6.116-qli-1.8-ver.1.1     ← 新包（名字不同！）
```

后果：

1. dpkg 认为是**两个不同的包**，不是升级
2. 新包装到 `/lib/modules/6.6.116-qli-1.8-ver.1.1/`
3. **旧包和旧 .ko 全部留着**（`/lib/modules/6.6.116-qli-1.7-ver.1.1/` 不会被删）
4. 磁盘浪费：kernel-module 约 17MB + Image 约 10MB，每变一次多一份
5. `modules.dep` 只对当前运行内核生成，旧版本的残留文件没有索引，`modprobe` 不会误用，但要手动清

处理办法（选一个）：

```bash
# 设备上看有多少份残留
ls -d /lib/modules/*/

# 清掉旧版本（确认当前内核版本后）
uname -r                                  # 6.6.116-qli-1.8-ver.1.1
ls -d /lib/modules/*/ | grep -v "$(uname -r)"

# 用 apt 清掉旧包名（包名要列出来，apt 不会自动 do）
dpkg -l 'kernel-module-*-6.6.116-qli-1.7-ver.1.1' | awk '/^ii/{print $2}' \
  | xargs -r apt-get purge -y
```

**建议**：只在真的要出新固件基线时才改 `DISTRO_VERSION`。日常迭代驱动改 `PR`。

### C. Firmware（`linux-firmware-*`）

```bash
# recipe: layers/meta-qcom-hwe/recipes-kernel/linux-firmware/linux-firmware_20251125.bb
# 1. 更新 SRCREV / 换成新的日期版本的 recipe
# 2. 新日期通常自带新 PV（20251125 → 20251126）
#    如果要原地改内容，同样加 PR = "r1"

bitbake linux-firmware -c package_write_deb -f
./quectel_build/tools/deb-publish.sh --from-lists
```

注意 firmware 带 **epoch `1:`**（`1:20251125-r0`），见下面"版本比较真值"。

## dpkg 版本比较真值（实测，别猜）

```
6.6-r0         <  6.6-r1          ✅ 可升级
6.6-r0         <  6.6-r10         ✅ 可升级（r10 > r1，不是字符串比较）
6.6-r0         >= 6.6             ❌ 源里用裸 6.6 永远升不了
0.git-r0       <  6.6-r0          ✅ 可升级（能离开 0.git 前缀）
1:20251125-r0  <  1:20251126-r0   ✅ 可升级
1:20251125-r0  >= 20251126-r0     ❌ epoch 丢了 → 判为降级，apt 忽略
6.6~rc1        <  6.6             ✅ 可升级（~ 是预发布，排更前）
```

自检命令：

```bash
dpkg --compare-versions "<镜像里的版本>" lt "<源里的版本>" \
  && echo "可升级" || echo "不会被升级"
```

镜像里的版本：`adb shell dpkg-query -W -f='${Version}\n' <包名>`
源里的版本：  `apt-cache policy <包名> | grep Candidate`

## 迭代流程（不需要重建镜像）

关键点：**`PUBLISH` 清单只在 `do_image` 时写**。只重建单个 recipe 时它还是旧的，
所以要用 `--from-lists` 让发布脚本现场解析。

```bash
# 1. 改代码 + 递增 PR/版本（见上面三类）
# 2. 只重建那个组件
bitbake <recipe> -c package_write_deb -f

# 3. 现场解析列表 + 依赖闭包，发布
./quectel_build/tools/deb-publish.sh --from-lists

# 4. 上传（你们现在的做法：整个目录传上去）
rsync -av --delete quectel_build/deb/ <server>:<webroot>/quectel/

# 5. 设备上升级
adb shell 'apt update'
adb shell 'apt-cache policy <包名>'          # 看 Candidate 是不是新版本
adb shell 'apt install --only-upgrade <包名>'
adb shell 'dpkg -l <包名> | tail -1'         # 确认版本
```

只有出正式固件包时才需要 `bitbake quecpi-image && buildpackage`。

## 半自动迭代：一条命令（推荐）

改了组件之后，用 `deb-iterate.sh` 把「递增版本 + 重建 + 发布」一次做完：

```bash
./quectel_build/tools/deb-iterate.sh msm-va-driver
./quectel_build/tools/deb-iterate.sh virtual/kernel
./quectel_build/tools/deb-iterate.sh msm-va-driver linux-firmware   # 多个一起
```

它做四件事：

1. 递增该组件在 `layers/meta-quectel/conf/deb-track/versions.inc` 里的 PR
   （`r0 → r1 → r2 …`）
2. 只重建这个 recipe：`bitbake <target> -c package_write_deb -f`
3. **校验**新 deb 的 `Version` 真的带上了新 PR（否则说明 recipe 没接
   `PR = "${@d.getVar('DEB_PR') or 'r0'}"`，直接报错并回滚 PR）
4. 调 `deb-publish.sh --from-lists` 发布（默认保留历史，降级可用）

失败安全：第 2 步 bitbake 失败会把 PR **回滚**，不会留下无意义的版本号空洞。

### 版本为什么必须靠一个集中表管理

apt 只比版本号，不比文件内容。重建后版本号不变，apt 就会说
"already the newest version"，板子继续用旧文件。

手改每个 recipe 的 PR 容易漏。所以版本集中在一个文件：

```
layers/meta-quectel/conf/deb-track/versions.inc

DEB_PR:pn-msm-va-driver = "r0"
DEB_PR:pn-linux-qcom-custom = "r0"
DEB_PR:pn-linux-firmware = "r0"
```

配方侧只需要一行（已接好）：

```bitbake
PR = "${@d.getVar('DEB_PR') or 'r0'}"
```

已接入的组件：

| 组件 | 接入位置 |
|---|---|
| `msm-va-driver` | `recipes-multimedia/msm-va-driver/msm-va-driver_1.0.bb` |
| 内核（连带 684 个 module） | `recipes-kernel/linux/linux-qcom-custom_6.6.bbappend` |
| firmware 全部子包 | `recipes-kernel/linux-firmware/linux-firmware_%.bbappend` |

这三个用 bbappend/直接改 recipe，**没有动 `meta-qcom-hwe` 上游 layer**。

### 新增一个可迭代组件

```bash
# 1. 在 versions.inc 加一行
DEB_PR:pn-<recipe-pn> = "r0"

# 2. 在 recipe（或本 layer 的 bbappend）里加一行
PR = "${@d.getVar('DEB_PR') or 'r0'}"
```

不用改 `deb-iterate.sh`。

## 降级：默认就支持

`quectel_build/deb/` **默认保留历史版本**，`Packages` 索引扫描目录里所有 deb，
所以 apt 天然看得到旧版本，直接就能降：

```bash
apt install <pkg>=<old-version>
```

不需要额外开关。目录不会因为重复构建而膨胀——**同版本文件直接覆盖**，只有版本
真的变了才多一份。当前基线约 726 个包 / **103 MB**。

增长量只跟实际改动成正比：

| 改了什么 | 每轮新增 |
|---|---|
| 只改 msm-va-driver | +0.2 MB（一个 deb）|
| 改内核（684 个 module） | +约 27 MB |
| 改 firmware | +数十 MB |

代价换来的是随时可回退。如果确实需要瘦身（比如服务器磁盘紧），可选：

```bash
# 只留当前闭包，丢弃所有历史（降级能力同时丢失）
./quectel_build/tools/deb-publish.sh --from-lists --replace-all
```

### 包被从列表移除时要注意

保留历史模式下，一个包如果从 `conf/deb-track/*.txt` 里删掉（或者包名因
`DISTRO_VERSION` 变化而改变），它**仍然留在目录里**，apt 也就仍然能看到它。

脚本会主动提示：

```
[deb-publish] note: 684 deb(s) in quectel_build/deb are not produced by the
              current lists (kept for downgrade). Examples:
[deb-publish]        kernel-module-btqca-6.6.116-qli-1.7-ver.1.1_6.6-r0_arm64.deb
              remove them with: --replace-all
```

看到这段就知道该不该 `--replace-all` 了。

### 上传时的一致性

你们整个目录上传是对的。保持一条原则：

**服务器上的 deb 集合必须和 `Packages` 索引一致。**

- 本地默认（保留历史）→ 服务器也要全传（含历史）→ 一致 ✓
- 传的时候用 `rsync --delete` → 服务器只留本次内容 → 与索引一致 ✓
- 本地清空发布、服务器却留着旧文件（或反之）→ **不一致，降级会失败**

## 回退

```bash
# 方式 1：用 keep-history 发布时的旧版本
adb shell 'apt install <包名>=<旧版本>'

# 方式 2：改 recipe 回到旧 SRCREV，PR 再 +1（不能减，版本必须单调递增）
#   PR = "r3"   ← 内容回到 r1 的状态，但版本比 r2 大，才能装上去
```

版本号**不能回退**（apt 会当降级拒绝），回退内容必须配一个更大的版本号。

## 验证清单（每次迭代都跑）

```bash
# 1. 新 deb 真的生成了，版本对
ls -la build-qcom-wayland/tmp-glibc/deploy/deb/*/<pkg>_*.deb
dpkg-deb -f <deb> Version Package

# 2. 发布脚本的现场解析没漏包（对比 class 的闭包数）
./quectel_build/tools/deb-publish.sh --from-lists 2>&1 | grep resolved
#   期望：resolved 726 package(s) from lists

# 3. 索引自洽（脚本会自动查，失败会 ERROR 退出）
grep -c '^Package: ' quectel_build/deb/Packages
ls quectel_build/deb/*.deb | wc -l          # 两者必须相等

# 4. 源里能看到新版本
apt-cache policy <包名>                      # Candidate 要变成新版本

# 5. 设备升级后确认
adb shell 'dpkg -l <包名> | tail -1'
adb shell 'dpkg -S <该包提供的文件>'         # 归属正确
```

### 内核模块迭代的额外验证

```bash
adb shell 'uname -r'                                    # 运行中的内核版本串
adb shell 'ls /lib/modules/'                            # 有没有多份残留
adb shell 'depmod -a'                                   # 重建索引（升级后应自动跑）
adb shell 'modinfo <模块名> | head -3'                   # vermagic 要和 uname -r 一致
adb shell 'modprobe <模块名> && dmesg | tail'            # 真能加载
```

**`vermagic` 必须等于运行内核的版本串**，否则 `modprobe` 报
`Invalid module format`。所以 `kernel-module-*` 和 `kernel-image-*` 必须同版本
一起升，不能只升一半。

## msm-va-driver 的完整示例

```bash
# 改驱动代码
edit layers/meta-quectel/recipes-multimedia/msm-va-driver/files/src/decode.c
# 递增版本
#   msm-va-driver_1.0.bb 里加:  PR = "r1"
bitbake msm-va-driver -c package_write_deb -f
ls build-qcom-wayland/tmp-glibc/deploy/deb/*/msm-va-driver_*
#   msm-va-driver_1.0-r1_arm64.deb
./quectel_build/tools/deb-publish.sh --from-lists
adb shell 'apt update && apt install --only-upgrade msm-va-driver'
adb shell 'vainfo | head'        # 硬解还能工作
```

它装两个路径（`/usr/lib/dri/` 和 `/usr/lib/aarch64-linux-gnu/dri/`），
同一个 deb 内的文件，升级时一起换，不会只换一半。

## 常见错误

| 症状 | 原因 | 处理 |
|---|---|---|
| `apt` 说已是最新，但设备上文件没变 | 版本没递增 | 加 `PR = "r<更大>"` 重发 |
| `apt-cache policy` 看不到新版本 | 上传后没 `apt update`，或 Packages 里没这个包 | 设备 `apt update`；本地查 `grep '^Package: <名>' Packages` |
| 升级后 `modprobe` 报 `Invalid module format` | 模块和内核版本串不一致 | 同时升 `kernel-image-*` |
| `/lib/modules` 下堆了多个版本 | 改了 `DISTRO_VERSION` 导致包名变化 | 见 B2，purge 旧包名 |
| `deb-publish.sh` 报 "no .deb for: xxx" | 那个 recipe 还没重建，或拼写错 | 先 `bitbake <recipe>`，再查 `deploy/deb` |
| `deb-publish.sh` 报 "Packages lists N but dir holds M" | 索引与文件不一致（并发上传/残留） | 默认模式重跑一遍（会清空重建） |
| `do_image` 报 `can only concatenate str` | deb-track.bbclass 的变量遮蔽 bug（已修） | 确认 class 里冲突预检用 `pkgname` 不是 `base` |

## 相关文件

| 文件 | 作用 |
|---|---|
| `quectel_build/tools/deb-publish.sh` | 发布（`--from-lists` / `--keep-history`） |
| `quectel_build/compile/build.sh` → `buildpackage()` | 固件打包时自动调发布 |
| `layers/meta-quectel/conf/deb-track/*.txt` | 要登记/发布的包列表 |
| `layers/meta-qcom-hwe/recipes-kernel/linux/linux-qcom-custom_6.6.bb` | 内核 SRCREV / PR |
| `layers/meta-qcom-distro/conf/distro/include/qcom-base.inc` | `DISTRO_VERSION`（决定包名） |
| `layers/meta-quectel/recipes-multimedia/msm-va-driver/msm-va-driver_1.0.bb` | msm-va-driver SRCREV / PR |
