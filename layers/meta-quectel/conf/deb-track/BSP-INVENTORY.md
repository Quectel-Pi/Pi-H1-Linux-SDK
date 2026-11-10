# BSP 清单：能登记进 dpkg 数据库的内容

本文件是"B 方案"（内核 + firmware）落地后，对整个 BSP 的盘点。
数据来自 2026-11 QCS6490 / Debian 13 trixie 实机。

## 已纳入 deb-track

| 列表 | 内容 | 包数（含闭包） |
|---|---|---|
| `kernel.txt` | 内核镜像 + 684 个 .ko + modules.* | 686 |
| `firmware.txt` | 本板实际固件的 35 个包 | 35 |
| `bsp-userspace.txt` | GStreamer 高通插件、PAL 音频、ONNX/QNN/NPU、Sensors、视觉、Quectel 工具 | 186 |

实测安装结果（2026-11 上板）：**1012 个包全部 `ii`，零文件冲突，
`apt-get check` 返回 0。**

依赖闭包会伸向 Yocto 自己的基础库，所以有两份判据文件：
`distro-owned.txt`（5875 个发行版已有包名）和
`distro-owned-paths.txt.gz`（179103 条发行版已拥有路径）。
遇到就保留发行版那份。详见 SKILL.md 的「安全边界」一节。

以下是 BSP 全貌的初步盘点，加入 `bsp-userspace.txt` 后无主文件数已
大幅下降，本表尚未按最新数据重算。

## BSP 全貌（按"能否登记"分类）

实机 `comm` 统计（117179 条 dpkg 已登记路径 vs 131184 个盘上文件）：
40669 个文件无主。按大类：

| 路径 | 无主文件数 | 状态 | 说明 |
|---|---|---|---|
| `/usr/lib/modules` | 15080 | ✅ 已纳入 | 661 个 .ko + modules.* + build/ |
| `/usr/lib/firmware` | 587 | ✅ 已纳入 | 578 有包，9 无包 |
| `/usr/lib/camera` | 100 | ⬜ 未纳入 | 相机 sensor/ISP 用户态 .so（camx） |
| `/usr/lib/dsp` | 75 | ⬜ 未纳入 | adsp/cdsp/dsp 用户态 |
| `/etc/sensors` | 106 | ⬜ 未纳入 | 传感器 registry/config/proto |
| `/etc/configs` | 38 | ⬜ 未纳入 | NPU/推理 JSON 配置 |
| `/etc/acdbdata` | 10 | ⬜ 未纳入 | 音频 ACDB 校准数据 |
| `/usr/libexec` | 402 | ⬜ 未纳入 | 大部分是 git-core(187)/valgrind(128)，非 BSP |
| `/usr/include` | 1514 | ⬜ 不建议 | 开发头文件，设备上无用 |
| `/usr/lib/python3.12` | 4913 | ⬜ 不建议 | Yocto python，与 Debian 冲突 |
| `/usr/share/games` | 4102 | ⬜ 不建议 | 无关内容 |
| `/usr/lib/gcc` | 473 | ⬜ 不建议 | 交叉编译器运行时 |

## 为什么 BSP 用户态（camx/dsp/sensors）没纳入

实测依赖检查（用 Debian 13 的 1502 个包 + 446 个 Provides 比对）：

```
camx-kt:  15 依赖，Debian 能满足 6，缺 9
  缺: qcom-sensinghub, property-vault, camxlib-kt, fastrpc,
      libprotobuf25.8.0, libabsl-log-internal-*, qcom-sensors-core
camxlib-kt: 10 依赖，缺 3
  缺: cameradlkm, fastrpc, qcom-adreno
qcom-adreno: 缺 libdmabufheap0, property-vault, wayland
```

原因是 Yocto 的依赖名体系与 Debian 不同（`libgcc1` vs `libgcc-s1`、
`libprotobuf25.8.0` 这类带版本号的私有名）。这些包只能靠
`--force-depends` 装进去，代价是 `dpkg --audit` 长期报未满足依赖、
`apt -f install` 清不掉。

**如果以后要纳入**，建议先决定：
1. 接受 `--force-depends` 的告警；或
2. 在源服务器上额外提供一批 shim 包（`qcom-sensinghub` 等空包）来闭环依赖。

## 必须知道的安全边界：DEB_TRACK_CLOSURE_DENY

firmware 包的 Depends 里有 `libc6`、`libstdc++6`、`shadow`、`base-files`
这类基础系统包名。如果让闭包去解析，就会把 **Yocto 版** 的这些包装到
Debian rootfs 上。实测危害：

```
Yocto libc6  -> /usr/lib/ld-linux-aarch64.so.1  （普通文件）
Debian  libc6 -> /usr/lib/ld-linux-aarch64.so.1  （软链 -> aarch64-linux-gnu/...）
```

Yocto 版会把动态链接器替换成真实文件，**可能导致镜像无法启动**。

所以 class 里有 `DEB_TRACK_CLOSURE_DENY`（35 条模式）硬边界，
`libc6`/`base-files`/`shadow`/`libstdc++6`/`libpam*`/`libxcb*`/`wayland`
等全部禁止进入闭包。这些依赖保持"未满足"，由 `--force-depends` 放行，
真实库本来就在 Debian rootfs 上。

验证方式（每次改列表都该跑）：

```bash
# 闭包里绝不能出现这些
dpkg -l 2>/dev/null | grep -E '^ii  (libc6|base-files|shadow|libstdc\+\+6|wayland):'
```

装完后 `libc6` 的版本必须是 Debian 的（`2.41-12+deb13u4`），
不能变成 Yocto 的 `2.39+git0+...`。

## 文件冲突自检

两个包提供同一路径时，dpkg 会成功 unpack 后一个、然后**静默回滚**成
`not-installed`（日志里只有 `half-installed` → `not-installed`）。
实测踩到 2 处：

| 文件 | 冲突双方 | 保留 |
|---|---|---|
| `qcom/venus-5.4/venus.mbn` | `qcom-video-firmware` vs `linux-firmware-qcom-venus-5.4` | 前者（还带 vpu-2.0/3.0） |
| `lt9611uxc_fw.bin` | `quectel-hdmi-firmware` vs `linux-firmware-lt9611uxc` | 前者 |

class 里已加 pre-flight 检查：批量 install 前扫一遍所有包的
`dpkg-deb --contents`，有重复路径直接 `bb.fatal` 并列出冲突对。

## 9 个无主的 firmware 文件

这些文件在盘上，但**没有任何 Yocto 包提供**，登记不了、也无从升级：

```
/usr/lib/firmware/bdwlan30.bin
/usr/lib/firmware/eeprom_qca9377_1p1_WB396_olpc.bin
/usr/lib/firmware/otp30.bin
/usr/lib/firmware/qca/tfbtfw11.tlv
/usr/lib/firmware/qca/tfbtnv11.bin
/usr/lib/firmware/qwlan30.bin
/usr/lib/firmware/utf30.bin
/usr/lib/firmware/wlan/qcom_cfg.ini
/usr/lib/firmware/wlan/wlan_mac.bin
```

要升级它们只能手写 deb（`wlan_mac.bin` 是板级 MAC，通常不该升级）。

## 加新列表的流程

1. 实机上确认路径真的无主：
   ```bash
   dpkg -S <path>   # 应报 "no path found"
   ```
2. 找出哪个包提供它（在 build 目录）：
   ```bash
   for f in $DEPLOY_DIR_DEB/*/*.deb; do
     dpkg-deb -c "$f" | grep -q 'path/to/file' && echo "$f"
   done
   ```
3. 检查该包的 Depends 是否会被 `DEB_TRACK_CLOSURE_DENY` 破坏
4. 加进对应列表
5. 检查文件冲突（class 会自检）
6. 实测：`ii` 数 == 列表闭包数，且 `libc6` 仍是 Debian 版本
