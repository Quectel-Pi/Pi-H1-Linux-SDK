# deb-track: 把 Yocto 产出的 deb 登记进 Debian/Ubuntu 的 dpkg 数据库

## 问题

`quecpi-image.bb` 先用 Yocto 装出 rootfs（`/var/lib/dpkg` 由 apt 写入），
然后 `deploy_debian_gnome_rootfs()` 用 rsync 把预制的 Debian/Ubuntu
tarball 覆盖上去。那个 tarball 自带 `/var/lib/dpkg`，于是 Yocto 装的
东西——整个 `/usr/lib/modules`、QCOM firmware、BSP 用户态——文件还在盘上，
但**数据库条目没了**：

```
# dpkg -S /usr/lib/modules/6.6.116-qli-1.7-ver.1.1/kernel/drivers/bluetooth/btqca.ko
dpkg-query: no path found matching pattern ...
```

后果是 apt 永远升不了这些文件，将来搭建源服务器也没法用 apt 升级。

`deb-track` 在 rsync 之后把 Yocto 产出的 deb **再装一遍**进目标 rootfs
的数据库。文件已经就位，所以这一步纯粹是补登记，让它们变成
"包管理的"，后续可以从 Quectel apt 源升级。

## 用法

镜像配方里：

```bitbake
inherit deb-track
DEB_TRACK_PACKAGES = "file://conf/deb-track/kernel.txt"
```

列表文件是纯文本，一行一个包名，`#` 起注释，允许 glob：

```
kernel-image-6.6.116-qli-1.7-ver.1.1   # 内核镜像（闭包带上 kernel-image-image/vmlinux）
kernel-6.6.116-qli-1.7-ver.1.1         # modules.builtin / modules.order
kernel-module-*                        # 684 个 .ko，含 kernel/ camera/ extra/
```

**加包就往列表里加一行**，不用改 class。

变量：

| 变量 | 默认 | 说明 |
|---|---|---|
| `DEB_TRACK_PACKAGES` | 空 | 列表文件，可多个 |
| `DEB_TRACK_DEPLOY_DIR` | `${DEPLOY_DIR_DEB}` | deb 来源目录 |
| `DEB_TRACK_ENABLED` | `1` | 设 `0` 跳过整个环节 |
| `DEB_TRACK_FATAL` | `1` | 出错是否让构建失败 |

## 实测结果

QCS6490 / Debian 13 trixie / kernel 6.6.116：

```
deb-track: kernel-module-*  -> 684 package(s)
deb-track: 688 deb(s) selected (686 listed, closure expanded)
deb-track: registered 687 package(s)
```

验收：

```
dpkg -l | grep -c '^ii  kernel'                    => 687
dpkg -l | grep kernel | grep -vc '^ii'             => 0
/usr/lib/modules/<ver>/modules.dep                 => 由 depmodwrapper 生成
dpkg -S .../kernel/drivers/bluetooth/btqca.ko      => kernel-module-btqca-...
dpkg -S .../camera/camera_qcm6490.ko               => kernel-module-camera-qcm6490-...
dpkg -S .../updates/r8168.ko                       => kernel-module-r8168-...
dpkg -S .../modules.builtin                        => kernel-6.6.116-...
dpkg -S /boot/Image-<ver>                          => kernel-image-image-...
```

耗时：unpack 687 个 deb 约 24 秒，configure 约 30 秒。全程无 pseudo、无 chroot、无 root。

## 机制要点（踩过的坑）

### 1. 必须排在 rsync 之后

`deploy_debian_gnome_rootfs` 挂在 `IMAGE_PREPROCESS_COMMAND`（`do_image`
阶段），不是 `ROOTFS_POSTPROCESS_COMMAND`（`do_rootfs` 阶段，早于 overlay 且
rsync 会把数据库再冲一遍）。

`:append` 按解析顺序拼接，class 在配方头部 inherit、配方的 append 在尾部，
所以直接 `IMAGE_PREPROCESS_COMMAND:append` 会落在 overlay **前面**。
本 class 用 `do_image[prefuncs]` 在读取变量前把两个函数插到
`deploy_debian_gnome_rootfs` 后面，幂等。

### 2. 用 build 自己的 dpkg-native，但**不要用 pseudo**

用 recipe sysroot 里的 `dpkg-native`（1.22，poky 打了补丁）：
`--force-script-chrootless` 让维护脚本不 chroot，并 `export D=<rootfs>`，
于是脚本走 `else`/`D 非空` 分支，和 `do_rootfs` 里完全一样。

但**不要套 pseudo**（也不要靠 image 任务的 fakeroot）。实测：

```
tar: ./control: Cannot change ownership to uid 0, gid 0: Operation not permitted
dpkg-deb: error: tar subprocess returned error exit status 2
```

原因：dpkg-deb 把 control 成员解到 `<admindir>/tmp`，再经系统 `tar` 管道；
tar 的 `chown()` 不经 pseudo 拦截，所以必失败。pseudo 还会在
`*.dpkg-tmp` 改名上 `abort()ing pseudo client ... path mismatch`。

去掉 pseudo、只用 `--force-not-root` 就一切正常：
**687 个包，unpack 24 秒 + configure 约 30 秒**。
负载文件属主不重要——文件是 rsync 放的，之后 `image.bbclass` 会
`chown -R root:root`。

`PATH` 必须补上 kernel recipe 的 `recipe-sysroot/usr/bin/crossscripts`
（`depmodwrapper` 在这），否则 `kernel-*` 的 postinst 报 127。

### 3. `statoverride` 要临时挪走

`/var/lib/dpkg/statoverride` 里有 7 条记录，涉及 Debian 专有账号
（`_chrony`、`geoclue`、`ssl-cert`、`messagebus`），宿主的 NSS 解析不了：

```
dpkg: unrecoverable fatal error, aborting:
 unknown system group '_chrony' in statoverride file
```

做法：跑之前 `cp` 到工作目录并 `rm`，`finally` 里恢复。这些 override 描的是
Debian 包的文件（`/etc/chrony`、`/var/lib/geoclue` 等），和本次登记的
内核包无关。

### 4. 维护脚本不能删

一开始想"文件已在盘上，脚本没用"直接剥掉，结果
`/usr/lib/modules/<ver>/modules.dep` **没了**——它不在任何 deb 里，是
`depmodwrapper` 跑出来的。而且设备上 `apt upgrade` 也要靠这些脚本。

所以脚本原样保留。实测 685 个 postinst 在 chrootless 模式下正常执行。

### 5. `Version: git-r0` 必须改写

11 个包（`kernel-module-cnss2`、`cnss-*`、`icnss2`、`msm-kgsl`、
`nt36xxx-i2c`、`qca6490`、`wlan`、`wlan-firmware-service`）的 Version 是
`git-r0`。dpkg 能装，但 Debian 版本号必须以数字开头，`apt`/`dpkg-query`
之后会解析失败，永远升不了。

只在**私有副本**里把 `git-r0` 改成 `0.git-r0`，`deploy/deb` 原产物不动。

## 已知限制

1. **依赖里非 Yocto 构建的部分不满足**。Yocto 的依赖名是 `libgcc1`、
   `libprotobuf25.8.0`、`qcom-sensinghub`，Debian 侧根本不存在（Debian 叫
   `libgcc-s1`）。用 `--force-depends` 装进去，库本身已经在 rootfs 上。
   代价：`dpkg --audit` 会看到这些未满足依赖，不能直接用
   `apt -f install` 清理。
2. **`depmod: ignoring bad line starting with 'exclude'`**：Yocto 生成的
   `/usr/lib/depmod.d/exclude.conf` 内容是 `exclude .debug`，KMOD 新版期望
   `override`/`search` 语法。设备上现有固件同样有这行，属既有现象。
3. **升级时版本号必须递增**。现在登记的是 `6.6-r0`，将来源服务器上的包
   必须是 `6.6-r1` 以上。firmware 登记的是 `1:20251125-r0`（带 epoch），
   源里**不能丢 epoch**，否则 apt 判为降级、永远不升。

   同包名升版本**不需要** `Replaces:`/`Breaks:`。只有**换包名接管文件**时
   才需要，且**只写 `Replaces:`、不写 `Breaks:`**（实测：写 `Breaks` 会报
   `would break ..., and deconfiguration is not permitted`）。详见 SKILL.md
   的“Replaces / Breaks”一节。
4. **ESP 上的 UKI 不在本 class 覆盖范围**。设备上 `/dev/sda1`
   （`PARTLABEL=efi`）默认不挂载，UKI 在 `/EFI/Linux/linux-qcm6490-idp.efi`。
   做内核升级包时维护脚本必须自己挂 ESP，否则会写到 btrfs 的 `/boot`，
   看着成功但下次启动不生效。

## 调试

```
# 只看这一环做了什么
bitbake quecpi-image -c image -f 2>&1 | grep deb-track

# 装完后在 rootfs 里核对
dpkg --root=<rootfs> --admindir=<rootfs>/var/lib/dpkg -l | grep kernel
```

构建目录里的中间产物：`${WORKDIR}/deb-track/`（改写后的 deb、`dpkg.log`、
pseudo 状态）。

## 发布成 apt 源（buildpackage 自动做）

`buildpackage` 末尾会调 `quectel_build/tools/deb-publish.sh`，把 **deb-track 实际
装进镜像的那批包** 发布成 apt 仓库：

```
quectel_build/deb/
    Packages, Packages.gz, Release     apt 索引
    *.deb                              包本体
    DISTRIBUTION                       人看的说明
```

只发列表里的（`conf/deb-track/*.txt` + 闭包），不会拷 deploy/deb 的 11117 个包。

`PUBLISH` 清单由 `deb_track_install` 写在 `${WORKDIR}/deb-track/PUBLISH`，
一行一个 `改写后的 deb <TAB> 文件名`，发布脚本只读它。

### 用法

```bash
# 构建时自动（buildpackage 已接好）
buildpackage

# 或单独重发（不重新编译）
./quectel_build/tools/deb-publish.sh
```

### 放到 HTTP 服务器

```bash
cp -r quectel_build/deb /var/www/html/quectel
```

板子上：

```bash
echo 'deb [trusted=yes] http://<host>/quectel ./' \
    > /etc/apt/sources.list.d/quectel.list
apt update
apt-cache policy kernel-module-btqca-6.6.116-qli-1.7-ver.1.1   # 能看到候选版本
apt install linux-firmware-qcom-qcm6490-audio
```

`[trusted=yes]` 是因为 deb 没签名。要公开就把 Release 签名（`Release.gpg` /
`InRelease`）并改用 `signed-by=`。

### 发布脚本的校验

- `Packages` 里的 `Package:` 条数必须等于目录里的 deb 数（防截断，截断会让 apt 看不到包）
- `gzip -dc Packages.gz` 必须和 `Packages` 完全一致
- `Packages` 里每个 `Filename:` 都必须在盘上存在
- **默认保留历史版本**（降级能力默认开启）：同版本文件覆盖，版本变了才多一份；
  因此目录里会累积旧版本，索引扫描全部，`apt install <pkg>=<old>` 可用
- 需要瘦身时用 `--replace-all` 清空重建（降级能力同时丢失）
- 包从列表移除或改名后，旧 deb 仍留在目录里并继续可见，脚本会打印
  `note: N deb(s) ... are not produced by the current lists` 提示

### 三种用法

```bash
# 1) buildpackage 自动调（读 PUBLISH，与镜像严格一致）
./quectel_build/tools/deb-publish.sh

# 2) 迭代：只重建了某个 recipe，现场解析列表（不等 PUBLISH）
./quectel_build/tools/deb-publish.sh --from-lists

# 3) 瘦身：丢掉历史版本（降级能力丢失）
./quectel_build/tools/deb-publish.sh --from-lists --replace-all
```

自动递增版本 + 重建 + 发布的一条命令：`quectel_build/tools/deb-iterate.sh`，
详见 `.pi/skills/deb-iterate/SKILL.md`。

`apt-ftparchive` 优先用宿主的（apt-utils），退化到 Yocto native sysroot。
两者产物实测逐字节一致，但 native 版在缺 `apt.conf.d` 时会刷一行
`E: Errors apply to file './Packages'` 的假错误，宿主版干净。

实测验证（2026-11，3 个包的最小仓库）：

```
apt-get update -> Get Release 947 B, Packages 1,171 B, Reading package lists... OK
apt-cache policy v4l2loopback -> Candidate: 0.13.2-r0, 500 file:///... OK
```

## 升级到设备前须知

源里的版本必须大于镜像里登记的版本，否则 `apt` 会认为已是最新：

| 场景 | 镜像登记版本 | 源里必须是 |
|---|---|---|
| 内核 | `6.6-r0` | `6.6-r1` 以上（不要用裸 `6.6`，比 `6.6-r0` 小） |
| firmware | `1:20251125-r0` | `1:20251126-r0` 以上（**epoch 不能丢**） |
| 曾经是 `git-r0` 的包 | 被改写成 `0.git-r0` | `0.git-rN`，或正常数字版本 |

`Replaces:` / `Breaks:` 的使用（实测结论）：

| 场景 | 需要？ | 结果 |
|---|---|---|
| 同包名升版本 | **不需要** | 正常 |
| 换包名接管文件 | **只写 `Replaces:`** | 成功 |
| 换包名 + 写 `Breaks:` | — | **失败**：`would break ..., deconfiguration not permitted` |
