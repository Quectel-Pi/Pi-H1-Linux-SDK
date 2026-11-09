# deb-track: 把 Yocto deb 登记进 Debian/Ubuntu dpkg 数据库

面向接手这个 SDK 的其他 agent。目标：让 Debian/Ubuntu 固件里那些
"文件在盘上、但 apt 不认识"的 BSP 内容变成**可 apt 升级**的包。

## 什么时候用

设备上出现这种输出，就是本 skill 的场景：

```
$ dpkg -S /usr/lib/modules/6.6.116-qli-1.7-ver.1.1/kernel/drivers/bluetooth/btqca.ko
dpkg-query: no path found matching pattern ...
```

量化无主文件（设备上跑）：

```bash
for p in $(dpkg-query -W -f='${Package}\n'); do dpkg-query -L "$p"; done \
  | sed 's|^/$||' | grep -v '^/$' | sort -u > /tmp/owned.txt
find / -xdev \( -path /proc -o -path /sys -o -path /run -o -path /tmp \
  -o -path /var/lib/dpkg -o -path /mnt/top -o -path /var/persist \) -prune \
  -o -type f -print 2>/dev/null | sort -u > /tmp/ondisk.txt
comm -13 /tmp/owned.txt /tmp/ondisk.txt | wc -l
```

2026-11 QCS6490/Debian 13 实测：131184 个文件里 **40669 个无主**。

## 根因（不要猜，就是这个链路）

`quecpi-image.bb`：

1. `do_rootfs`：Yocto 用 apt 装 deb，`/var/lib/dpkg` 有完整记录；
2. `do_image` → `IMAGE_PREPROCESS_COMMAND` → `deploy_debian_gnome_rootfs`：
   rsync 预制 Debian tarball，**覆盖了 `/var/lib/dpkg`**（排除列表里没有它）。

文件是第 1 步装的，数据库是第 2 步的。所以要在 rsync **之后**把 deb 再装一遍。

## 现状（已实现，直接用）

```
layers/meta-quectel/classes/deb-track.bbclass    机制
layers/meta-quectel/conf/deb-track/kernel.txt    内核列表
layers/meta-quectel/conf/deb-track/firmware.txt  firmware 列表
layers/meta-quectel/conf/deb-track/README.md     机制文档
layers/meta-quectel/conf/deb-track/BSP-INVENTORY.md  BSP 盘点
quectel_build/tools/deb-publish.sh               apt 仓库发布
```

配方里（`quecpi-image.bb` 已有）：

```bitbake
inherit deb-track
DEB_TRACK_PACKAGES = " \
    file://conf/deb-track/kernel.txt \
    file://conf/deb-track/firmware.txt \
"
```

**加包 = 往列表加一行**（支持 `kernel-module-*` 这种 glob），不用改 class。

当前规模：kernel 688 + firmware 52 → 合并闭包 **728 个包**，实测全部 `ii`。

## 设计总览

### 数据流

```
Yocto do_rootfs                       apt 装 deb，/var/lib/dpkg 完整
        ↓
do_image
  IMAGE_PREPROCESS_COMMAND
    deploy_debian_gnome_rootfs        rsync 预制 tarball，覆盖 /var/lib/dpkg  ← 断点
    deb_track_prepare                 [读列表] → 解析 → 闭包 → DEB_TRACK_RESOLVED
    deb_track_install                 [改写副本] → dpkg 装进目标库 → 写 PUBLISH
        ↓
  do_image 打包 system.img            根文件系统（含数据库）
        ↓
buildpackage
  a_key_generation.sh                 固件包
  deb-publish.sh                      [读 PUBLISH] → quectel_build/deb/（apt 源）
```

### 两个入口，职责分开

| 阶段 | 函数 | 干什么 |
|---|---|---|
| `do_image` prefunc | `deb_track_order_hook` | 把自己插到 overlay 之后（幂等） |
| `do_image` | `deb_track_prepare` | 读列表、匹配 deb、算依赖闭包，结果存 `DEB_TRACK_RESOLVED` |
| `do_image` | `deb_track_install` | 改写副本 → 预检冲突 → dpkg unpack/configure → 校验 → 写 `PUBLISH` |
| `buildpackage` | `deb-publish.sh` | 读 `PUBLISH` → 生成 `Packages`/`Release` |

### 关键设计决定（为什么这么做）

**1. 用 `prepare` / `install` 两段，不用一个函数**

`prepare` 只做"读和算"（纯 Python，不碰 rootfs），`install` 才做"写"。
好处：列表解析错误、包找不到、闭包为空，都在碰 rootfs 之前就 `bb.fatal`，
不会留下半残的 rootfs。

**2. 加工（改写副本）而不是原地改**

`resolved`（deploy/deb 原产物）**永不修改**，所有改写发生在
`${WORKDIR}/deb-track/debs/` 的副本上。所以：

- 不会污染 `deploy/deb`（其他 recipe 可能还在用）
- 可以重复执行（每次 `rmtree` 干净重建）
- 能 A/B：同一份 deploy/deb 配不同列表

**3. 只改 `Version`，不动维护脚本和文件内容**

镜像里的包必须和将来仓库里发的包**行为一致**。所以只修唯一"不改就没法
升级"的东西（非法版本号），脚本和 payload 原样保留。

**4. `unpack` 和 `configure` 分两次调用**

先全部 `--unpack`，再 `--configure -a`。这样 dpkg 一次看到完整包集合，
自己排序依赖，不会因为"依赖还没 unpack"而乱序失败。

**5. `statoverride` 挪走而不是伪造 NSS**

伪造 NSS（pseudo）会连带搞坏 `dpkg-deb` 解包（见坑 2）。挪走只影响
构建期对 `setuid` 文件的权限处理，而 `image.bbclass` 之后会
`chown -R root:root` 兜底。

**6. 依赖闭包只在 `deploy/deb` 内解析 + 硬黑名单**

不查 Debian 的 dpkg 库。原因：一旦允许往 Debian 的库里找，就会把
Yocto 版的 `libc6` 等拉进来（见"安全边界"），风险远大于收益。
黑名单是最后一道闸。

**7. 发布靠 `PUBLISH` 清单传递，不靠重新扫描**

`buildpackage` 阶段如果重新扫 `deploy/deb`，就会把 11117 个包都发出去。
所以 `install` 阶段写下"实际装进镜像的"清单，发布只读它。
这也是"只放列表里的文件"的实现方式。

### 配置项

| 变量 | 默认 | 说明 |
|---|---|---|
| `DEB_TRACK_PACKAGES` | 空 | 列表文件，可多个；空则不做事 |
| `DEB_TRACK_DEPLOY_DIR` | `${DEPLOY_DIR_DEB}` | deb 来源 |
| `DEB_TRACK_ENABLED` | `1` | `0` 跳过整个环节 |
| `DEB_TRACK_FATAL` | `1` | 出错是否让构建失败 |
| `DEB_TRACK_PUBLISH` | `1` | 是否生成 `PUBLISH` 清单 |
| `DEB_TRACK_CLOSURE_DENY` | 36 条 | 禁止进入闭包的模式（安全边界） |

### 列表格式

纯文本，一行一个包名，`#` 注释，支持 glob：

```
# 精确名：只匹配 <name>_<ver>_<arch>.deb
kernel-image-6.6.116-qli-1.7-ver.1.1

# glob：展开成所有匹配（684 个模块）
kernel-module-*
```

- 精确名用 `<name>_*.deb`，**不会**误命中 `kernel-image` → `kernel-image-vmlinux`
- glob 同时试 `<pat>_*.deb` 和 `<pat>-*_*.deb` 并去重
- 自动排除 `-dbg_` `-dev_` `-staticdev_` `-src_` `-doc_` 变体
- 找不到的包名会汇总报错（`DEB_TRACK_FATAL=0` 时降为警告）

### 中间产物（调试用）

```
${WORKDIR}/deb-track/
  debs/                改写后的副本（实际装进去的）
  dpkg.log             dpkg 自己的日志
  PUBLISH              发布清单：<副本路径> \t <文件名>
  statoverride.bak     临时挪走的 statoverride
```

## 五个必须遵守的点

| # | 要点 | 违反的后果 |
|---|---|---|
| 1 | 必须排在 `deploy_debian_gnome_rootfs` **之后** | 白干，rsync 把数据库冲掉 |
| 2 | 用 recipe sysroot 的 `dpkg-native` + kernel 的 `crossscripts`；**不要用 pseudo** | pseudo 下 `dpkg-deb` 解 control 必失败 |
| 3 | 维护脚本**原样保留** | `modules.dep` 丢失；设备 `apt upgrade` 行为不一致 |
| 4 | `Version` 必须以数字开头 | `git-r0` 能装但 apt/dpkg-query 解析失败，永远升不了 |
| 5 | 跑前把 `statoverride` 挪走，跑完恢复 | `unknown system group '_chrony'` 直接 abort |

### 坑 1：`:append` 顺序反直觉

`deploy_debian_gnome_rootfs` 挂在 `IMAGE_PREPROCESS_COMMAND`（`do_image` 阶段），
**不是** `ROOTFS_POSTPROCESS_COMMAND`（那个在 `do_rootfs`，早于 overlay）。

`:append` 按解析顺序拼接：class 在配方头部 inherit、配方的 append 在尾部，
直接 `IMAGE_PREPROCESS_COMMAND:append` 会落在 overlay **前面**。用
`do_image[prefuncs]` 在变量被读取前插入（幂等）：

```python
python deb_track_order_hook() { _deb_track_ordering(d) }
do_image[prefuncs] += "deb_track_order_hook"
```

### 坑 2：为什么不能用 pseudo（这条花了最久）

```
tar: ./control: Cannot change ownership to uid 0, gid 0: Operation not permitted
dpkg-deb: error: tar subprocess returned error exit status 2
```

链条：`dpkg` → `dpkg-deb --control <deb> <admindir>/tmp/...` → 经**系统 tar**
管道 → tar 的 `chown()` 不经 pseudo 拦截 → 必失败。pseudo 还会在 dpkg 的
`*.dpkg-tmp` 改名上报 `path mismatch`。

**正解**：去掉 pseudo，只用 `--force-not-root --force-script-chrootless`。
（`--force-script-chrootless` 是 poky 给的补丁，会 `export D=<rootfs>`，
维护脚本走和 `do_rootfs` 完全相同的分支。）

负载文件属主不重要：文件是 rsync 放的，之后 `image.bbclass` 会
`chown -R root:root`。

被推翻过的错误结论（别再走）：

1. 「pseudo 能解决 statoverride」——能，但代价是解 deb 全挂。
2. 「`PSEUDO_IGNORE_PATHS` 排除整个工作树就行」——单包能过，全量不可复现。
3. 「`fakeroot` 行」——和 pseudo 一样拦不住系统 tar，且抢 `LD_PRELOAD`。

### 坑 3：`modules.dep` 不在任何 deb 里

它是 `depmodwrapper` 跑出来的。剥维护脚本 = 设备上没有 `modules.dep`，
`modprobe` 退化，而且设备 `apt upgrade` 行为会和镜像不一致。**别剥。**

### 坑 4：非法版本号只有 11 个包

```bash
for f in $DEPLOY_DIR_DEB/*/*.deb; do
  v=$(dpkg-deb -f "$f" 2>/dev/null | grep '^Version:' | cut -d' ' -f2)
  case "$v" in ""|[0-9]*) ;; *) echo "$(basename $f) => $v";; esac
done
```

实测：`kernel-module-{cnss2,cnss-nl,cnss-plat-ipc-qmi-svc,cnss-prealloc,
cnss-utils,icnss2,msm-kgsl,nt36xxx-i2c,qca6490,wlan,wlan-firmware-service}`
+ `qcacld-wlan*`、`qcom-graphicsdevicetree*`，都是 `git-r0`。

class 在**私有副本**里改成 `0.git-r0`，`deploy/deb` 原产物不动。

### 坑 5：`statoverride` 要临时挪走

`<rootfs>/var/lib/dpkg/statoverride` 涉及 Debian 专有账号
（`_chrony`、`geoclue`、`ssl-cert`、`messagebus`），宿主 NSS 解析不了：

```
dpkg: unrecoverable fatal error, aborting:
 unknown system group '_chrony' in statoverride file
```

跑前 `cp` 到工作目录并 `rm`，`finally` 里恢复。这些 override 描述的是
Debian 包自己的文件，和 BSP 包无关。

**注意**：这只是**构建期**需要。设备上有这些账号，`apt upgrade` 不会遇到。

## 安全边界：DEB_TRACK_CLOSURE_DENY

**这是硬边界，不是偏好。** firmware 包的 Depends 里有 `libc6`、`libstdc++6`、
`shadow`、`base-files`。若让闭包解析，会把 **Yocto 版**基础系统装到 Debian 上：

```
Yocto libc6  → /usr/lib/ld-linux-aarch64.so.1   普通文件
Debian libc6 → /usr/lib/ld-linux-aarch64.so.1   软链 → aarch64-linux-gnu/...
```

Yocto 版会替换动态链接器，**可能让镜像无法启动**。

class 里 36 条模式禁止 `libc6`/`base-files`/`shadow`/`libstdc++6`/`libgcc1`/
`libpam*`/`libxcb*`/`wayland` 等进入闭包。它们保持"未满足"，由
`--force-depends` 放行，真实库本来就在 Debian rootfs 上。

每次改列表都要验证：

```bash
# 装完后 libc6 必须是 Debian 版本，不能变成 Yocto 的 2.39+git0
dpkg -l libc6 | tail -1
# 期望：ii  libc6:arm64  2.41-12+deb13u4      ← Debian
# 错误：ii  libc6:arm64  2.39+git0+58cbbd43fe ← Yocto！
ls -la /usr/lib/ld-linux-aarch64.so.1   # 必须还是软链
```

## 文件冲突：两个包提供同一路径会被静默回滚

dpkg 会成功 unpack 后一个包，然后**静默回滚成 `not-installed`**，
日志里只有 `half-installed` → `not-installed`，很难发现。

实测踩到 2 处：

| 文件 | 冲突双方 | 保留 |
|---|---|---|
| `qcom/venus-5.4/venus.mbn` | `qcom-video-firmware` vs `linux-firmware-qcom-venus-5.4` | 前者（还带 vpu-2.0/3.0） |
| `lt9611uxc_fw.bin` | `quectel-hdmi-firmware` vs `linux-firmware-lt9611uxc` | 前者 |

class 已加 pre-flight 检查：批量装之前扫所有包的 `dpkg-deb --contents`，
有重复路径直接 `bb.fatal` 并列出冲突对。

自己查也可以：

```bash
python3 - <<'EOF'
import glob,subprocess,collections
own=collections.defaultdict(list)
for deb in glob.glob('build-qcom-wayland/tmp-glibc/deploy/deb/*/*.deb'):
    out=subprocess.run(['dpkg-deb','-c',deb],capture_output=True,text=True).stdout
    for l in out.splitlines():
        if l.startswith('d'): continue
        p=l.split()[5] if len(l.split())>=6 else ''
        if p.startswith('./'): p='/'+p[2:]
        if p and not p.endswith('/'): own[p].append(deb.split('/')[-1])
for p,v in own.items():
    if len(v)>1: print(p,v)
EOF
```

## 打包发布成 apt 源

`buildpackage` 末尾自动调 `quectel_build/tools/deb-publish.sh`。

**只发列表里的包**（列表 + 闭包），不会拷 `deploy/deb` 的 11117 个包。
依据是 `deb_track_install` 写在 `${WORKDIR}/deb-track/PUBLISH` 的清单。

### 设计要点

**清单传递而非重扫**。`PUBLISH` 一行一个：

```
<副本 deb 绝对路径>\t<发布用文件名>
```

发布脚本不碰 `deploy/deb`，也不重新解列表——所以"镜像里装了什么"和
"源里发什么"永远是同一批，不会漂。

**平铺目录 + `./` 源**。`apt-ftparchive packages .` 写出的 `Filename` 是
`./xxx.deb`，所以 sources.list 必须是 `deb ... ./`（不是 `deb ... /`）。
实测这个组合 apt 能正确解析。

**每次先清旧 deb**。发布前 `find -name '*.deb' -delete`，否则上一轮删掉的
包还会留在源里，apt 仍会把它当可选版本。

**`apt-ftparchive` 优先用宿主的**。Yocto native 版在缺 `apt.conf.d` 时刷假
错误 `E: Errors apply to file './Packages'`（产物其实完全正确，实测
Release 的 SHA256 与实际文件对得上），宿主版（apt-utils）干净。两者产物
逐字节一致，所以优先宿主、退化 native，两边的机器都能用。

**三条自检**（防"仓库看着成功，apt 却看不到包"）：

1. `Packages` 的 `Package:` 条数 == 发布数（防截断）
2. `gzip -dc Packages.gz` 与 `Packages` 完全一致
3. 发布数为 0 直接报错退出

产物：

```
quectel_build/deb/
    Packages, Packages.gz, Release     apt 索引
    *.deb                              包本体
    DISTRIBUTION                       说明
```

单独重发（不重编译）：

```bash
./quectel_build/tools/deb-publish.sh
```

生成过程的关键点：

- `Packages` 里 `Filename:` 是 `./xxx.deb` 相对形式 → sources.list 用 `deb ... ./`
- 优先用宿主 `apt-ftparchive`（apt-utils），退化到 Yocto native sysroot。
  两者产物**逐字节一致**，但 native 版缺 `apt.conf.d` 时会刷假错误
  `E: Errors apply to file './Packages'`，宿主版干净。
- 脚本自检三条：`Package:` 条数 == 发布数（防截断）、`Packages.gz` 与
  `Packages` 一致、每次发前清掉旧 `*.deb`（防 apt 看到过期包）。

部署到 HTTP 服务器：

```bash
cp -r quectel_build/deb /var/www/html/quectel
```

设备上：

```bash
echo 'deb [trusted=yes] http://<host>/quectel ./' \
    > /etc/apt/sources.list.d/quectel.list
apt update
apt-cache policy kernel-module-btqca-6.6.116-qli-1.7-ver.1.1
apt install linux-firmware-qcom-qcm6490-audio
```

`[trusted=yes]` 必须加，因为 deb 没签名。要公开就签 Release
（`Release.gpg`/`InRelease`）并改 `signed-by=`。

实测（3 个包的最小仓库）：

```
apt-get update  → Get: Release [947 B] / Packages [1,171 B] / Reading package lists... OK
apt-cache policy v4l2loopback → Candidate: 0.13.2-r0, 500 file:///...
```

## 版本号：升级能不能生效，全看这里

### 当前镜像里登记的版本（真值）

| 包类型 | 登记版本 |
|---|---|
| 内核 + 全部 .ko | `6.6-r0` |
| firmware | `1:20251125-r0`（带 epoch 1） |
| git-r0 那 11 个 | 被改写成 `0.git-r0` |

### dpkg 版本比较真值（实测 `dpkg --compare-versions`）

```
6.6-r0         < 6.6-r1        → 可升级
6.6-r0         < 6.6-r10       → 可升级（注意 r10 > r1，不是字符串比较）
6.6-r0        >= 6.6           → 不可升级！不要在源里用裸 6.6
0.git-r0       < 0.git-r1      → 可升级
0.git-r0       < 6.6-r0        → 可升级（离开 0.git 前缀是允许的）
1:20251125-r0  < 1:20251126-r0 → 可升级
1:20251125-r0 >= 20251126-r0   → 不可升级！epoch 不能丢
6.6~rc1        < 6.6           → 可升级（~ 表示预发布，排更前）
```

### 三条铁律

1. **版本必须严格递增**，源里 ≥ 镜像里的版本时 apt 认为"已是最新"，什么都不做。
2. **firmware 的 epoch 必须保留**。Epoch 优先级最高，`1:...` 永远大于
   任何不带 epoch 的版本。源里发 `20251126-r0`（无 epoch）会被判为降级，
   apt 直接忽略。
3. **`0.git-r0` 那批**要么继续用 `0.git-rN`，要么升到正常数字版本
   （`6.6-r0` > `0.git-r0`，是被允许的）。

### 发布前自检

```bash
# 镜像里登记的版本
adb shell dpkg-query -W -f='${Version}\n' kernel-module-btqca-6.6.116-qli-1.7-ver.1.1

# 源里的版本
apt-cache policy kernel-module-btqca-6.6.116-qli-1.7-ver.1.1 | grep Candidate

# 手工比一次
dpkg --compare-versions "<镜像版本>" lt "<源版本>" && echo "可升级" || echo "不会被升级"
```

### 发一个新版本的完整步骤

假设要把内核模块从 `6.6-r0` 升到 `6.6-r1`：

1. **改 recipe 的版本**（不是手改 deb）

   内核 recipe 是 `layers/meta-qcom-hwe/recipes-kernel/linux/linux-qcom-custom_6.6.bb`，
   它**没有显式 `PR`**，`6.6-r0` = `PV`(`6.6`) + 默认 `PR`(`r0`)。
   两种改法：

   ```bitbake
   # A) 只改包版本，不动源码（最简单，适合重新打一遍同样的二进制）
   PR = "r1"

   # B) 改源码基线（真正升级内核，会把 PV 变成 6.6+git<sha>）
   SRCREV = "<新的 commit>"
   ```

   注意 B 会让版本从 `6.6-r0` 变成 `6.6+git<hash>-r0`，
   两者用 `dpkg --compare-versions` 比一下确认是新的大再发。
2. **重新构建 deb**（只要包，不用出整镜像）
   ```bash
   bitbake virtual/kernel -c package_write_deb -f
   ```
3. **确认新版本号变了**
   ```bash
   ls build-qcom-wayland/tmp-glibc/deploy/deb/qcm6490_idp/kernel-module-btqca-*
   dpkg-deb -f <新 deb> Version      # 必须是 6.6-r1
   ```
4. **更新列表**（如果设备名变了才需要；只改版本不用动列表）
5. **构建镜像**（新版本也会装进镜像，两边版本对齐）
   ```bash
   bitbake quecpi-image
   ```
6. **发布源**
   ```bash
   ./quectel_build/tools/deb-publish.sh
   ```
7. **设备上验证**
   ```bash
   adb shell 'apt update && apt-cache policy kernel-module-btqca-6.6.116-qli-1.7-ver.1.1'
   adb shell 'apt install --only-upgrade kernel-module-btqca-6.6.116-qli-1.7-ver.1.1'
   adb shell 'dpkg -l kernel-module-btqca-6.6.116-qli-1.7-ver.1.1 | tail -1'   # 应是 6.6-r1
   ```

### 升级内核模块时容易踩的坑

1. **模块和内核必须同版本**。`vermagic: 6.6.116-qli-1.7-ver.1.1` 写在 .ko
   里，新 .ko 配旧内核会 `modprobe` 报 `Invalid module format`。
   所以 `kernel-module-*` 和 `kernel-image-*` 的 `Depends` 必须锁死同一个
   `kernel-<ver>`，不能只升一半。
2. **升内核 = 改 UKI + 改 DTB 分区**，不只是升 .ko。见下面 UKI 限制。
3. **`modules.dep` 会变**。旧的 `modules.dep` 指向旧 .ko 路径时，
   `modprobe` 会找不到。所以维护脚本里的 `depmod -a` 必须能跑（这就是
   不能剥脚本的另一个原因）。

## Replaces / Breaks：包名不变时不需要

实测结论（别照抄网上说法）：

| 场景 | 需要 Replaces？ | 结果 |
|---|---|---|
| **同名同包**升版本（`kpkg 6.6-r0` → `kpkg 6.6-r1`） | **不需要** | 正常，`over (6.6-r0)` |
| **换包名**接管文件（`old-kernel` → `new-kernel`） | **必须** | 不写：`trying to overwrite '/usr/lib/shared.ko', which is also in package ...` → 失败 |
| 换包名 + `Replaces` + `Breaks` | — | **反而失败**：`would break ..., and deconfiguration is not permitted` |
| 换包名 + **只写 `Replaces`** | **这样才对** | 成功，`Replacing files in old package ...` |

所以：

```
# 同包名升级（大多数情况）—— 什么都不用加
Package: kernel-module-btqca-6.6.116-qli-1.7-ver.1.1
Version: 6.6-r1

# 换包名接管文件 —— 只加 Replaces，不要加 Breaks
Package: quectel-kernel-modules
Version: 1.0-r1
Replaces: kernel-module-btqca-6.6.116-qli-1.7-ver.1.1
```

## 验收（改完必须跑）

```bash
DP=<sysroot-native>/usr/bin/dpkg
$DP --root=<rootfs> --admindir=<rootfs>/var/lib/dpkg --force-not-root \
    -l | grep -cE '^ii  (kernel|linux-firmware)'
$DP --root=<rootfs> --admindir=<rootfs>/var/lib/dpkg --force-not-root \
    -l | grep kernel | grep -vc '^ii'         # 必须是 0
$DP --root=<rootfs> --admindir=<rootfs>/var/lib/dpkg --force-not-root \
    -S /usr/lib/modules/<ver>/kernel/drivers/bluetooth/btqca.ko
```

2026-11 QCS6490 实测（728 个包）：

```
ii -> 728        非 ii -> 0
unpack 28s + configure 30s
modules.dep      -> 由 depmodwrapper 生成（不在任何 deb 里）
btqca.ko         -> kernel-module-btqca-6.6.116-...
camera_qcm6490.ko-> kernel-module-camera-qcm6490-...
r8168.ko         -> kernel-module-r8168-...
modules.builtin  -> kernel-6.6.116-...
/boot/Image-<v>  -> kernel-image-image-...
libc6            -> 仍是 Debian 的 2.41（不是 Yocto 2.39）
```

## 加更多包

列表按类别拆文件，别堆一个。候选（2026-11 实测无主计数）：

| 路径 | 无主数 | 建议 |
|---|---|---|
| `/usr/lib/modules` | 15080 | ✅ 已纳入 |
| `/usr/lib/firmware` | 587 | ✅ 已纳入 |
| `/usr/lib/camera` | 100 | ⬜ camx 用户态 |
| `/usr/lib/dsp` | 75 | ⬜ adsp/cdsp 用户态 |
| `/etc/sensors` | 106 | ⬜ 传感器 registry |
| `/etc/configs` | 38 | ⬜ NPU 推理 JSON |
| `/etc/acdbdata` | 10 | ⬜ 音频校准 |
| `/usr/include`、`python3.12`、`gcc`、`games` | ~11000 | ❌ 开发文件/与 Debian 冲突 |

**加之前先 `dpkg -S <path>` 确认真无主**。实测 overlay 里有 5 个文件与 Debian
包重叠（`/etc/gdm3/daemon.conf`、`/etc/systemd/logind.conf`、
`/etc/default/useradd`、`/usr/lib/systemd/system/power-profiles-daemon.service`），
别塞进 Yocto 包。

BSP 用户态（camx/dsp/sensors）**未纳入**的原因：依赖链闭环不了。

```
camx-kt: 15 依赖，Debian 只能满足 6，缺 9
  缺: qcom-sensinghub, property-vault, camxlib-kt, fastrpc,
      libprotobuf25.8.0, libabsl-log-internal-*, qcom-sensors-core
```

Yocto 的依赖名（`libgcc1` vs Debian `libgcc-s1`、`libprotobuf25.8.0` 这种
私有版本名）Debian 侧不存在，只能 `--force-depends`，代价是
`dpkg --audit` 长期报警、`apt -f install` 清不掉。

## 已知限制

1. 非 Yocto 依赖名永远不满足。内核/firmware 这批实测**依赖自洽**
   （156 个依赖名全部由登记包内部满足，Debian 侧 0、缺失 0），
   但 BSP 用户态不是。
2. 9 个 firmware 文件没有任何 deb 提供（`bdwlan30.bin`、`wlan_mac.bin`、
   `qca/tfbtfw11.tlv` 等，列在 `firmware.txt` 末尾）。要升级只能手写 deb。
   其中 `wlan_mac.bin` 是板级 MAC，本来就不该升级。
3. `depmod: ignoring bad line starting with 'exclude'`：Yocto 生成的
   `/usr/lib/depmod.d/exclude.conf` 语法与新版 KMOD 不符。设备现有固件也有，
   属既有现象，不阻塞。
4. **UKI 升级**：设备上 `/dev/sda1`（`PARTLABEL=efi`）默认不挂载，
   `/boot` 只是 system 分区上的普通目录。升内核的包**必须在 postinst 里
   自己挂 ESP**，否则 UKI 写到 btrfs 里，看着成功但下次启动不生效。

## 操作手册：要改东西时改哪里

### 想加一个 Yocto 组件（比如 msm-va-driver 这种 BSP 用户态）

BSP 用户态要**逐个 recipe 加**，别整批扫。每加一个走这 7 步：

**1. 确认它的文件真的无主**（设备上或 prebuild rootfs 里）

```bash
# 先看它装哪些文件
f=build-qcom-wayland/tmp-glibc/deploy/deb/armv8-2a/<pkg>_*.deb
dpkg-deb -c $f | grep -v '/$'

# 逐個确认无主（在设备上跑，或对 prebuild/debian-gnome-rootfs）
dpkg -S <path>          # 必须报 "no path found"
```

**2. 确认它不和 Debian 已有的包抢路径**

BSP 包经常同时往 `/usr/lib/` 和 `/usr/lib/aarch64-linux-gnu/` 各装一份
（msm-va-driver 就是这样）。两个都要查。

**3. 看 Depends 会不会把 Yocto 基础库拖进来**

```bash
dpkg-deb -I $f | grep -E '^(Depends|Pre-Depends):'
```

典型情况：`Depends: libc6 (>= ...), libva`。`libc6` 会被 deny 挡住，
但 **`libva` 不在默认 deny 里**。要不要挡，看这一步：

**4. 判断间接依赖能不能用 Debian 的代替**

```bash
# 看它真的 NEEDED 什么（dlopen 的依赖不会出现在 NEEDED 里）
dpkg-deb -x $f /tmp/x && readelf -d /tmp/x/.../foo.so | grep NEEDED
```

msm-va-driver 的实测结果：

```
NEEDED: libc.so.6, ld-linux-aarch64.so.1     ← 没有 libva
```

它靠 libva **运行时 dlopen** 加载，所以 Debian 的 `libva2 2.22`
（在 `/usr/lib/aarch64-linux-gnu/`）完全够用。而 Yocto 的 `libva` 是
2023 年的 2.20.0，装到 `/usr/lib/`（非 multiarch）会和 Debian 并存，
有被 dlopen 到旧版的风险 → **把 `libva` 加进 `DEB_TRACK_CLOSURE_DENY`**。

一般规律：

| 情况 | 怎么做 |
|---|---|
| Debian 已有同功能包且 ABI 兼容 | 把 Yocto 那个加进 deny，用 Debian 的 |
| Debian 没有，且 NEEDED 直接依赖 | 必须纳入闭包（不要 deny） |
| 只是 dlopen 的插件 | 取决于宿主包（如 libva）的版本兼容性 |

**5. 往 `bsp-userspace.txt` 加一行**

```
msm-va-driver
```

**6. 用预检脚本验证**（不开构建，几十秒）

跑 `### 想验证自己的改动` 里第 2 步的脚本，把列表改成三个：

```python
for lst in ('kernel.txt','firmware.txt','bsp-userspace.txt'):
```

期望：`MISSING: 无`、`危险包: 无`、`文件冲突: 无`，
且新包在闭包里、被 deny 的包不在。

**7. 真实构建 + 上板验收**

```bash
bitbake quecpi-image -c image -f 2>&1 | grep deb-track
adb shell dpkg -S /usr/lib/aarch64-linux-gnu/dri/msm_drv_video.so   # -> msm-va-driver
adb shell vainfo | head                                             # 硬件解码是否工作
```

#### msm-va-driver 实测结果（可以作为样板）

```
closure           720 个包（kernel 688 + firmware + msm-va-driver）
install           720/720 全部 ii，unpack 28s
两个路径都登记    /usr/lib/dri/                      -> msm-va-driver
                  /usr/lib/aarch64-linux-gnu/dri/   -> msm-va-driver
libva             仍是 Debian 的 libva2 2.22，/usr/lib/libva.so.2 不存在
libc6/loader      仍是 Debian 2.41 / 软链
```

且它的 README 正好写明了这就是 deb-track 要解决的场景：

> `msm-va-driver` 在 `do_rootfs` 阶段装进 Yocto rootfs，
> `deploy_debian_gnome_rootfs()` 随后把 Debian tarball rsync 上去
> （不带 `--delete`，Debian 也没有同名文件），所以 recipe 的文件
> 是最终发行的那个 —— 但 dpkg 数据库被覆盖，文件从此无主。

#### 注意 deb 可能不存在

`camxlib-kt` 就是教训：它曾在 `firmware.txt` 里，但复测时
`deploy/deb` 里已经没有这个 deb 了，`DEB_TRACK_FATAL=1` 会直接让构建失败。
所以：

- 加包前确认 `deploy/deb` 里真有这个 deb（先跑一次对应 recipe 的
  `-c package_write_deb`）
- 价值低且依赖不闭环的包（camxlib-kt 只带 1 个 firmware 文件，
  Depends 里的 `cameradlkm`/`fastrpc` 都不满足）别加
- 或者把它从列表拿掉，等依赖能闭环再登

### 想加一批包

只改列表，不动 class：

1. 设备上确认路径无主：`dpkg -S <path>` → 要报 `no path found`
2. 反查是谁提供的：
   ```bash
   for f in build-qcom-wayland/tmp-glibc/deploy/deb/*/*.deb; do
     dpkg-deb -c "$f" 2>/dev/null | grep -q 'path/to/file' && echo "$f"
   done
   ```
3. 看它的 Depends 会不会撞上 `DEB_TRACK_CLOSURE_DENY`
4. 往 `conf/deb-track/<类别>.txt` 加一行
5. 新类别就在 `quecpi-image.bb` 的 `DEB_TRACK_PACKAGES` 里追加一个 `file://`

### 想禁一个包进闭包

加进 `DEB_TRACK_CLOSURE_DENY`（bbclass 顶部，36 条模式）。
判断标准：该包会写 Debian 已经在管的路径（尤其 loader / libc / base 系统）。

### 想跳过整个环节做 A/B

```bash
bitbake quecpi-image -c image -f -R conf/…   # 或临时在配方里
DEB_TRACK_ENABLED = "0"
```

### 只想重发仓库，不重编译

```bash
./quectel_build/tools/deb-publish.sh
```
它只读上一次构建留下的 `PUBLISH`。

### 想验证自己的改动

**第 1 步：语法 + 解析**（几十秒，先过这关）

```bash
cd /home/igni/Downloads/debian/1.7-QuecPi-QCLinux-BL01
python3 -c "
import ast,re,textwrap
src=open('layers/meta-quectel/classes/deb-track.bbclass').read()
ok=True
for m in re.finditer(r'^def (\w+)\(.*?\n(?=\S)',src,re.S|re.M):
    try: ast.parse(m.group(0))
    except SyntaxError as e: print('SYN',m.group(1),e); ok=False
for m in re.finditer(r'^python (\w+)\(\) \{\n(.*?)^\}',src,re.S|re.M):
    try: ast.parse(textwrap.dedent(m.group(2)))
    except SyntaxError as e: print('SYN',m.group(1),e); ok=False
print('syntax ok' if ok else 'ERR')
"
bash -n quectel_build/tools/deb-publish.sh && echo "publish ok"
cd build-qcom-wayland && source ../layers/poky/oe-init-build-env . >/dev/null && \
    bitbake -e quecpi-image 2>&1 | grep -E '^ERROR:|^DEB_TRACK_PACKAGES='
```

**第 2 步：闭包 + 冲突预检**（不开构建，验证列表对不对）

这是最值的一步——能把”差点装了 Yocto libc6“和”两包文件重叠“在编译前就
暴露出来。把 class 里的纯函数抽出来跑：

```bash
cd /home/igni/Downloads/debian/1.7-QuecPi-QCLinux-BL01
python3 - <<'EOF'
import os,glob,re
src=open('layers/meta-quectel/classes/deb-track.bbclass').read()
s=src.index('def _deb_track_control('); e=src.index('# task bodies')-len('# ------')
exec(compile(src[s:e],'<c>','exec'), globals())
deploy='build-qcom-wayland/tmp-glibc/deploy/deb'
nb=os.path.dirname(glob.glob(
    'build-qcom-wayland/tmp-glibc/work/*/*/*/recipe-sysroot-native/usr/bin/dpkg')[0])
archs=['all','armv8-2a','qcm6490','qcm6490_idp']
m=re.search(r'DEB_TRACK_CLOSURE_DENY \?\?= " \\\n(.*?)"', src, re.S)
DENY=m.group(1).replace('\\','').split()
variants=('-dbg_','-dev_','-staticdev_','-src_','-doc_')
res=[]
for lst in ('kernel.txt','firmware.txt'):
    for line in open('layers/meta-quectel/conf/deb-track/'+lst):
        line=line.split('#')[0].strip()
        if not line: continue
        hits=set()
        for a in archs:
            pats=[line+'_*.deb'] + ([line+'-*_*.deb'] if glob.has_magic(line) else [])
            for p in pats: hits.update(glob.glob(os.path.join(deploy,a,p)))
        hits=[h for h in sorted(hits)
              if not any(os.path.basename(h).startswith(line+v) for v in variants)]
        res.extend(hits if glob.has_magic(line) else hits[-1:])
clo=_deb_track_closure(res,archs,deploy,nb,deny=DENY)
print('闭包:',len(clo))
bad=[d for d in clo if any(os.path.basename(d).startswith(x+'_') for x in
     ('libc6','base-files','shadow','libstdc++6','libgcc1','wayland'))]
print('危险包:',bad if bad else '无')
import subprocess,collections
own=collections.defaultdict(list)
for deb in clo:
    out=subprocess.run(['dpkg-deb','-c',deb],capture_output=True,text=True).stdout
    for l in out.splitlines():
        if l.startswith('d'): continue
        p=l.split()[5] if len(l.split())>=6 else ''
        if p.startswith('./'): p='/'+p[2:]
        if p and not p.endswith('/'): own[p].append(os.path.basename(deb))
dup={p:v for p,v in own.items() if len(v)>1}
print('文件冲突:',dup if dup else '无')
EOF
```

期望：`危险包: 无`、`文件冲突: 无`。

**第 3 步：真实构建后看日志**

```bash
bitbake quecpi-image -c image -f 2>&1 | grep deb-track
# 期望：deb-track: NNN package(s) registered
```

**第 4 步：上板验收**（见上面“验收”一节）。最关键两条：

```bash
adb shell dpkg -l libc6 | tail -1        # 必须是 Debian 2.41，不能是 Yocto 2.39
adb shell 'ls -la /usr/lib/ld-linux-aarch64.so.1'   # 必须还是软链
```

### 改动禁区

| 不要做 | 原因 |
|---|---|
| 改 `deploy/deb` 里的原产物 | 其他 recipe 在用，且丢失重建一致性 |
| 剥维护脚本 | `modules.dep` 会丢，设备升级行为变了 |
| 加回 pseudo / fakeroot | 解 deb 必挂（坑 2） |
| 在 `ROOTFS_POSTPROCESS_COMMAND` 里做 | 早于 overlay，白干 |
| 发布时重扫 `deploy/deb` | 会把 11117 个包都发出去 |
| 用 `6.6` / `20251126-r0` 这种丢 epoch 的版本发源 | apt 判为降级，永远不升 |

### 相关文件速查

| 文件 | 作用 |
|---|---|
| `layers/meta-quectel/classes/deb-track.bbclass` | 机制主体 |
| `layers/meta-quectel/conf/deb-track/kernel.txt` | 内核 3 行（含 glob） |
| `layers/meta-quectel/conf/deb-track/firmware.txt` | firmware 36 行 |
| `layers/meta-quectel/conf/deb-track/README.md` | 机制详解 |
| `layers/meta-quectel/conf/deb-track/BSP-INVENTORY.md` | BSP 全量盘点 |
| `quectel_build/tools/deb-publish.sh` | apt 仓库发布 |
| `quectel_build/compile/build.sh` → `buildpackage()` | 自动调用发布 |
| `layers/meta-quectel/conf/layer.conf` | 导出 `META_QUECTEL_DIR` |
