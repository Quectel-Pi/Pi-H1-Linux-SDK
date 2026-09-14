# OTA EFI 内核升级测试指南 (QuecPi H1 / QCS6490)

作者: Igni.Li / Hermes Agent
日期: 2026-08-06
适用固件: QSM565DWF (QCS6490) DEBIAN/WESTON 定制类型
关联组件: `layers/meta-quectel/recipes-quectel/quectel-ota/` (OTA 工具 + TimeCapsule GUI)

---

## 1. 目的

验证 OTA 升级组件 (`/usr/sbin/ota_run`) 的 **EFI 内核同步升级** 功能：

1. 把新内核 (UKI, linux-qcm6490-idp.efi) 通过 OTA 流程替换到 `/efi/EFI/Linux/`
2. 重启后设备从新内核启动 (`/proc/version` 可区分)
3. 验证 Btrfs 子卷 A/B 切换、自动回滚机制正常

本目录下提供 **uname 可区分** 的测试内核 (compat 方式):

| 文件 | /proc/version 特征 | 用途 |
|------|--------------|------|
| `linux-qcm6490-idp-otatest1-compat.efi` | `6.6.116-debug (ota@test1)` | OTA 升级目标 #1 |
| `linux-qcm6490-idp-otatest2-compat.efi` | `6.6.116-debug (ota@test2)` | OTA 升级目标 #2 |

> 注: 出厂/常规构建的内核版本为 `6.6.116-debug`。
>
> ⚠️ **为什么用 compat 方式**: OTA 只替换 `/efi` 里的 UKI, **不替换 rootfs 里的 `/lib/modules` 内核模块**。
> 内核模块按 uname -r (release 段) 匹配目录加载, 一旦 release 段变了 (如 -debug → -otatest1),
> 模块全部加载失败 → 显示驱动 msm_display 起不来 → **屏幕黑屏** (实测踩过, 见第 12 节)。
> compat 方式保持 release 段 `6.6.116-debug` 不变, 只改 `(ota@testX)` 编译标记 (不参与模块 vermagic),
> 因此模块照常加载、屏幕正常, 同时可用 `/proc/version` 区分。

---

## 2. 前置条件

1. 设备已完成工厂初始化: 存在 `/var/persist/fct_done_flag`
   ```bash
   adb shell ls /var/persist/fct_done_flag
   # 若不存在:
   adb shell touch /var/persist/fct_done_flag
   ```
2. 设备根文件系统是 Btrfs (`/dev/sda3`)
   ```bash
   adb shell mount | grep "on / "
   ```
3. adb 可连接 (root 权限):
   ```bash
   ./quectel_build/tools/adb devices
   ./quectel_build/tools/adb shell id   # 应显示 uid=0(root)
   ```

---

## 3. 首次部署 (ota_run init)

> 注意: 只有**首次**需要在顶层根文件系统 (subvol=/) 执行 init。
> init 会: 备份内核 → 创建 @V1 快照 → 重启进入子卷模式。

```bash
adb shell ota_run init
# 设备自动重启, 等待回来
adb wait-for-device

# 验证已进入子卷模式
adb shell ota_run current     # 期望输出: /@V1
adb shell mount | grep " on / "   # subvol=/@V1
```

> 若已初始化过 (current 输出 /@V1 等), 跳过本步骤。

---

## 4. 升级前准备

### 4.1 确认当前内核版本

```bash
adb shell cat /proc/version
# 出厂: Linux version 6.6.116-debug (oe-user@oe-host) ...
```

### 4.2 建立备份快照 (安全网)

```bash
adb shell ota_run backup
# 期望: EFI kernel backup SUCCESS / Backup snapshot @backup created successfully
```

### 4.3 准备升级脚本 (可选)

设备无外网时, 默认 `/opt/system_upgrade.sh` 会卡在 wait_for_network。
测试内核升级建议换成 no-op 脚本 (原脚本会备份为 .bak):

```bash
# 在宿主机写一个 no-op 脚本
cat > /tmp/ota_noop_upgrade.sh <<'EOF'
#!/bin/sh
echo "===== OTA TEST no-op upgrade ====="
exit 0
EOF

adb push /tmp/ota_noop_upgrade.sh /tmp/
adb shell ota_kernel_install install-script /tmp/ota_noop_upgrade.sh
```

---

## 5. 执行 OTA 升级 (内核替换)

### 5.1 放置测试内核

```bash
# 方法 A: 用设备自带工具 (推荐, 自动算 md5 并自检, 文件名随意)
adb push log/linux-qcm6490-idp-otatest1-compat.efi /tmp/
adb shell ota_kernel_install /tmp/linux-qcm6490-idp-otatest1-compat.efi
# 期望: [OK] 内核升级文件已就绪

# 方法 B: 手动放置 (需同时提供 .efi 和 .md5, 且 efi 需命名为固定名)
adb push log/linux-qcm6490-idp-otatest1-compat.efi /opt/kernel_upgrade/linux-qcm6490-idp.efi
adb shell "md5sum /opt/kernel_upgrade/linux-qcm6490-idp.efi > /opt/kernel_upgrade/linux-qcm6490-idp.efi.md5"
```

> OTA 检测条件: `/opt/kernel_upgrade/linux-qcm6490-idp.efi` + `.md5` 同时存在且 md5 校验通过。

### 5.2 执行升级 (前台运行, 约 30~90 秒后自动重启)

```bash
adb shell ota_run upgrade @V2
```

关键日志节点 (出现即代表内核升级路径执行):
```
[STEP 4] Update kernel from worksubvol /opt
[INFO] New kernel + MD5 detected, updating...
EFI kernel restore SUCCESS
[STEP 6] current_vol updated: next boot WORK_SUBVOL=@V2
[STEP 9] Upgrade Finished
Rebooting.
```

> 注意: 不要用 `nohup`/`setsid` 在 adb 里后台跑 (此板 adbd 会杀会话后代)。
> 直接前台跑即可, adb 断开等待重启回来。

### 5.3 重启后验证

```bash
adb wait-for-device   # 等待重启
# 等待 shell 就绪 (轮询 echo up)

# 1) 子卷切换
adb shell ota_run current            # 期望: /@V2
adb shell mount | grep " on / "      # subvol=/@V2

# 2) 内核标记变化 (核心验证点! 用 /proc/version, uname -a 不显示标记段)
adb shell cat /proc/version
# 期望: Linux version 6.6.116-debug (ota@test1) ...
#                                    ^^^^^^^^^  标记已变, release 不变

# 3) EFI 文件确实被替换
adb shell md5sum /efi/EFI/Linux/linux-qcm6490-idp.efi
md5sum log/linux-qcm6490-idp-otatest1-compat.efi   # 两边应一致

# 4) /opt 内核备份同步更新
adb shell md5sum /opt/linux-qcm6490-idp.efi

# 5) 屏幕正常 (模块兼容)
adb shell ls /sys/class/drm/   # 应有 card0 和 card0-DSI-1
```

### 5.4 如何鉴别升级是否成功 (判定清单)

**✅ 成功 = 以下全部满足:**

| # | 鉴别点 | 命令 | 成功标志 |
|---|--------|------|---------|
| 1 | 内核标记变化 (最直接) | `adb shell cat /proc/version` | 出现 `(ota@test1)`, release 仍是 `6.6.116-debug` |
| 2 | 子卷已切换 | `adb shell ota_run current` | 输出 `/@V2` (升级时指定的名字) |
| 3 | 根挂载在新子卷 | `adb shell mount \| grep " on / "` | `subvol=/@V2` |
| 4 | EFI 文件确实被替换 | `adb shell md5sum /efi/EFI/Linux/linux-qcm6490-idp.efi` | 与 log/ 里 otatest1-compat.efi 的 md5 一致 |
| 5 | 版本标记已写入 | `adb shell cat /version` | 输出 `@V2` |
| 6 | 回滚标记已清除 | `adb shell ls /restore_flag` | 文件不存在 (升级成功才删) |
| 7 | 系统能正常启动 | 设备能进桌面/服务起来 | adb shell 可用 |
| 8 | 屏幕正常 (模块兼容) | `adb shell ls /sys/class/drm/` | 有 card0 和 card0-DSI-1 |

**❌ 失败/未生效的信号:**
- `/proc/version` 没有 `(ota@test1)` 标记 → 内核没替换 (检查 /opt/kernel_upgrade 是否被清、md5 是否配对)
- `ota_run current` 还是 `/@V1` → 子卷没切 (升级中途失败)
- 升级日志出现 `[ERROR] ... rebooting for rollback` → 自动回滚被触发
- 重启后进不了系统 → initramfs 自动从 @backup 恢复 (看串口日志的 restore 提示)

**🔧 快速一键鉴别 (升级重启后执行):**
```bash
adb shell "cat /proc/version | grep -o '(ota@test[0-9]*)'; ota_run current; md5sum /efi/EFI/Linux/linux-qcm6490-idp.efi"
# 期望依次输出:
# (ota@test1)
# /@V2
# <与 log/linux-qcm6490-idp-otatest1-compat.efi 相同的 md5>
```

---

## 6. 第二次升级 (验证可反复升级)

重复第 5 节, 换成 otatest2-compat 内核:

```bash
adb push log/linux-qcm6490-idp-otatest2-compat.efi /tmp/
adb shell ota_kernel_install /tmp/linux-qcm6490-idp-otatest2-compat.efi
adb shell ota_run upgrade @V3
# 重启后:
adb shell cat /proc/version   # 期望: ... (ota@test2) ..., release 仍 6.6.116-debug
adb shell ota_run current     # /@V3
```

---

## 7. 回滚 / 恢复出厂内核

```bash
# 回滚到 @V1 (含恢复 @V1 备份的内核)
adb shell ota_run rollback @V1
# 重启后 cat /proc/version 应回到 (oe-user@oe-host) 或原标记 (若 @V1 备份的是出厂内核)

# 查看所有子卷
adb shell ota_run list

# 手动恢复出厂内核 (init 时备份的紧急镜像)
adb shell "cp /efi/EFI/Linux/linux-qcm6490-idp-original.efi /efi/EFI/Linux/linux-qcm6490-idp.efi"
```

---

## 8. 升级失败自动回滚机制

```
升级开始 → 设置 restore_flag → 执行升级脚本
   ├─ 成功 → 删除 restore_flag → 切换子卷 → 重启
   └─ 失败 → 重启 → initramfs 检测 restore_flag
                → 从 @backup 恢复 → 重启进 @backup
```

验证方法: 故意放一个损坏/不匹配的内核 (md5 错), 升级应失败并自动回滚。

---

## 9. 测试方法 (compat 内核)

> compat 内核保持 release 不变 (6.6.116-debug), 用 `(ota@testX)` 编译标记区分, 模块兼容不黑屏。

### 9.1 准备测试内核 (compat 版)

```bash
# 从 log/ 选一个 compat 内核推送到设备 (一次只放一个)
adb push log/linux-qcm6490-idp-otatest1-compat.efi /tmp/

# 用设备自带工具安装到 /opt/kernel_upgrade/ (自动以固定名复制 + 算 md5 + 自检)
adb shell ota_kernel_install /tmp/linux-qcm6490-idp-otatest1-compat.efi
# 期望: [OK] 内核升级文件已就绪 ... md5:5e133ad7e63dd59feeabb0343cfb2931
```

### 9.2 执行升级

```bash
# 前台执行 (adb 断开会杀掉后台进程, 不要 nohup/setsid), 约 30~90 秒后自动重启
adb shell ota_run upgrade @V2
# 关键日志:
#   [STEP 4] New kernel + MD5 detected, updating...
#   EFI kernel restore SUCCESS
#   [STEP 6] current_vol updated: next boot WORK_SUBVOL=@V2
```

### 9.3 重启后鉴别成功 (验证重点: 读取 /proc/version)

```bash
adb wait-for-device

# 核心验证: /proc/version 的 (ota@testX) 段, 而 release 仍是 6.6.116-debug
adb shell cat /proc/version
# 期望: Linux version 6.6.116-debug (ota@test1) ...
#                                    ^^^^^^^^^  标记已变, release 不变
# (注意: uname -a 不显示 (ota@testX) 段, 必须用 /proc/version 确认!)

# 子卷切换
adb shell ota_run current          # /@V2

# EFI 文件确实是推送的 compat 内核
adb shell md5sum /efi/EFI/Linux/linux-qcm6490-idp.efi
md5sum log/linux-qcm6490-idp-otatest1-compat.efi   # 两边一致

# 屏幕应正常亮 (模块兼容)
adb shell ls /sys/class/drm/        # 应有 card0 和 card0-DSI-1
```

**成功标志总结**: `/proc/version` 出现 `(ota@test1)`, release 仍 `6.6.116-debug`, 屏幕亮, 子卷切到 @V2。

### 9.4 第二次升级 (验证可反复升级)

```bash
adb push log/linux-qcm6490-idp-otatest2-compat.efi /tmp/
adb shell ota_kernel_install /tmp/linux-qcm6490-idp-otatest2-compat.efi
adb shell ota_run upgrade @V3
# 重启后: cat /proc/version 显示 (ota@test2), current = /@V3, 屏幕亮
```

### 9.5 回滚 (恢复旧内核)

```bash
adb shell ota_run rollback @V1
# 重启后: cat /proc/version 回到 (oe-user@oe-host) 或原标记, current = /@V1
```

---

### 产物清单 (2026-08-06 已生成, 位于 log/)

| 文件 | /proc/version 特征 | md5 | 大小 |
|------|--------------|-----|------|
| `linux-qcm6490-idp-otatest1-compat.efi` | 6.6.116-debug (ota@test1) | 5e133ad7e63dd59feeabb0343cfb2931 | 49541632 |
| `linux-qcm6490-idp-otatest2-compat.efi` | 6.6.116-debug (ota@test2) | 147d28bc4518a191545167cb69ac9339 | 49541632 |
| `efi.bin-otatest1-compat.vfat` | (内含 test1 UKI) | - | 524288000 |
| `efi.bin-otatest2-compat.vfat` | (内含 test2 UKI) | - | 524288000 |

- 配套 md5 文件: `linux-qcm6490-idp-otatestX-compat.efi.md5` 等。
- OTA 测试用 `linux-qcm6490-idp-otatest1-compat.efi` 或 otatest2-compat,
  设备端 `cat /proc/version` 显示 `(ota@testX)`, release 保持 6.6.116-debug, 屏幕正常。

> ⚠️ **文件名说明**: 设备端 OTA 检测的文件名是**固定**的 `linux-qcm6490-idp.efi`
> (`/opt/kernel_upgrade/linux-qcm6490-idp.efi` + 同名 `.md5`), 替换目标也是固定名
> `/efi/EFI/Linux/linux-qcm6490-idp.efi` (见 ota_run.sh / ota_kernel_install.sh)。
> **log/ 里的存档名 `linux-qcm6490-idp-otatestX-compat.efi` 无需手动改名**,
> 用 `ota_kernel_install` 工具时它内部自动以固定名复制 + 重算 md5 + 自检:
> - 推荐: `adb shell ota_kernel_install /tmp/linux-qcm6490-idp-otatest1-compat.efi`
> - 手动: push 后 `cp` 为 `/opt/kernel_upgrade/linux-qcm6490-idp.efi` 并重算同名 md5。
> 注意 md5 文件名必须与 efi 同名配对 (`linux-qcm6490-idp.efi.md5`), 否则 OTA 检测不到。

---

## 10. 常见问题

| 现象 | 原因 | 解决 |
|------|------|------|
| `[ERROR] System not ready` | 缺 fct_done_flag | `adb shell touch /var/persist/fct_done_flag` |
| 升级脚本卡住 (wait_for_network 死循环) | 设备无外网 | 用 no-op 升级脚本 (见 4.3) |
| `ota_run upgrade` 后台跑失败 | adbd 杀会话后代 | 前台运行, 不要 nohup/setsid |
| 内核升级未执行 | 缺 .efi 或 .md5 | 两个文件都要放 `/opt/kernel_upgrade/` |
| 升级后 uname 没变 | 内核未真正替换 | 检查 /opt/kernel_upgrade 是否被清理; 用 md5sum 对比 |
| 升级后起不来 | 内核损坏 | 自动回滚 @backup; 或手动恢复 original.efi |
| **升级后屏幕黑屏, 但系统在跑 (Xorg/GNOME 都在)** | **OTA 只换 /efi UKI, 不换 /lib/modules; 新内核 release 变了 → 模块 vermagic 不匹配 → msm_display 加载失败 → 无 DRM card** | **用 compat 内核 (release 不变, 改 (ota@testX) 标记); 已黑屏则 `ota_run rollback` 回旧内核** |

---

## 11. 测试记录 (2026-08-06)

| 项目 | 结果 |
|------|------|
| ota_run init → @V1 子卷 | ✅ 成功, subvolid=256 |
| ota_run backup → @backup | ✅ 成功 |
| upgrade @V2 + 标记内核 | ✅ 全流程成功, 自动重启 |
| 重启后子卷切换 | ✅ /@V2 (subvolid=259) |
| EFI 内核替换 | ✅ md5 从 c006a7c1... → dcd0955f... |
| /version 标记 | ✅ @V2 |
| restore_flag 清除 | ✅ |
| boot_attempts 清理 | ✅ (A/B fallback 握手正常) |
| history_ver 机制 | ✅ 旧 @V1 从 @backup 重建为历史版本 |
| 版本号方式升级后屏幕黑屏 | ⚠️ 模块不匹配 (见第 12 节), 已回滚 @V2 恢复 |
| rollback @V2 | ✅ 内核回滚 + 屏幕恢复亮 |
| 编译 compat 内核 (ota@test1/test2) | ✅ 保持 release 6.6.116-debug, 仅改编译标记 |
| compat 内核 OTA 实测 (upgrade @V4) | ✅ /proc/version 显示 (ota@test1), 屏幕亮, 不黑屏 |

> 注: 首轮测试用的"带标记内核" (尾部追加 25 字节) 仅用于 md5 区分, /proc/version 不变。
> compat 内核通过 KBUILD_BUILD_USER/HOST 改 `(ota@testX)` 标记, release 不变, 模块兼容不黑屏。

---

## 12. 黑屏问题实录 (版本号方式教训, 2026-08-06)

**现象**: 用版本号方式的内核 (release 改为 6.6.116-otatest1) OTA 升级后, 屏幕黑屏, 但系统在跑 (adb 能进, Xorg/GNOME 进程都在)。

**诊断过程**:
```
# /sys/class/drm/ 无任何 card (只有 version)
ls /sys/class/drm/          → version

# 模块目录只有旧版本
ls /lib/modules/            → 6.6.116-debug  (没有 6.6.116-otatest1)

# 显示驱动模块加载失败
modprobe msm_display        → FATAL: Module msm_display not found in directory /lib/modules/6.6.116-otatest1

# lsmod 无 msm → DRM 不创建 → 屏幕无输出
```

**根因**: OTA 组件的"EFI 内核同步升级"只替换 `/efi/EFI/Linux/linux-qcm6490-idp.efi` (UKI),
**不替换 rootfs 里 `/lib/modules/<version>/` 的内核模块**。内核模块按 uname -r 匹配目录,
新内核 release 从 `6.6.116-debug` 变成 `6.6.116-otatest1` 后, 模块全部加载失败,
显示驱动 msm_display 起不来, 无 DRM card, 屏幕黑屏。

**模块 vermagic 决定因素** (验证):
```
modinfo msm_display.ko | grep vermagic
vermagic: 6.6.116-debug SMP preempt mod_unload aarch64
# 只匹配 release 段 + 编译选项, 不含编译者/主机/时间戳
```

**解决**: `adb shell ota_run rollback @V2` 回滚到旧内核 (release 6.6.116-debug 与模块匹配),
屏幕恢复。

**避免**: 用 compat 内核 — 保持 release 不变, 只改 `(ota@testX)` 标记
(KBUILD_BUILD_USER/HOST, 不参与 vermagic)。`/proc/version` 仍可区分, 但模块兼容、屏幕正常。
