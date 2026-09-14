# QuecPi OTA (Over-The-Air) 升级组件

## 概述

QuecPi OTA 是一个基于 Btrfs 子卷快照的系统升级方案，支持 A/B 分区升级、自动回滚和版本管理。

### 核心特性

- **A/B 分区升级**: 使用 Btrfs 子卷实现无缝系统升级
- **自动回滚**: 升级失败时自动恢复到备份版本
- **内核同步升级**: 支持同时升级 EFI 内核
- **版本管理**: 支持多版本切换和历史版本保留
- **数据完整性**: MD5 校验确保文件一致性

## 文件结构

```
quectel-ota/
├── quectel-ota.bb                    # BitBake 配方
├── initramfs-rootfs-image.bbappend   # initramfs 镜像配置
├── initramfs-framework_1.0.bbappend  # initramfs 框架扩展
└── files/
    ├── ota                           # initramfs OTA 启动脚本
    ├── ota_run.sh                    # 主 OTA 命令行工具
    ├── system_upgrade.sh             # 系统升级脚本模板
    ├── quectel-ota.service           # systemd 服务配置
    └── current_vol                   # 当前活跃子卷记录
```

## 系统架构

### Btrfs 子卷布局

```
/dev/sda3 (Btrfs 分区)
├── @V0        # 工作子卷 (当前运行系统)
├── @V1        # 工作子卷 (升级后系统)
├── @backup    # 备份子卷 (用于回滚)
└── opt/       # 共享配置目录
    ├── current_vol      # 记录当前活跃子卷
    ├── history_ver      # 历史版本记录
    └── linux-qcm6490-idp.efi*  # 内核备份
```

### 启动流程

```
┌──────────────────────────────────────────────────────────────┐
│                        开机启动                               │
└──────────────────────┬───────────────────────────────────────┘
                       │
                       ▼
┌──────────────────────────────────────────────────────────────┐
│  initramfs 阶段 (92-ota 脚本)                                │
│  - 挂载 Btrfs 顶层                                           │
│  - 读取 current_vol 确定启动子卷                              │
│  - 检测 restore_flag                                         │
│  - 挂载对应子卷作为根文件系统                                  │
└──────────────────────┬───────────────────────────────────────┘
                       │
           ┌───────────┴───────────┐
           │                       │
           ▼                       ▼
┌─────────────────────┐   ┌─────────────────────┐
│  正常启动            │   │  恢复模式            │
│  (WORK_SUBVOL)       │   │  (BACKUP_SUBVOL)     │
└─────────────────────┘   └─────────────────────┘
```

## 命令行工具使用

OTA 工具安装在 `/usr/sbin/ota_run`，提供以下命令：

### 初始化 (首次使用必须执行)

```bash
sudo ota_run init
```

**功能**:
- 备份当前内核到 `/opt/`
- 创建初始 Btrfs 子卷快照 `@V0`
- 自动重启进入子卷模式

**执行时机**: 系统首次部署时，或需要重新初始化备份功能时

### 查看当前子卷

```bash
sudo ota_run current
```

**输出示例**:
```
/@V0
```

### 创建备份快照

```bash
sudo ota_run backup
```

**功能**:
- 备份当前 EFI 内核到 `/opt/`
- 创建 `@backup` 子卷快照
- 用于升级前的安全备份

### 系统升级

```bash
sudo ota_run upgrade @V2
```

**参数**: `@V2` - 新子卷名称，必须以 `@` 开头

**升级流程**:
1. 创建当前系统的备份快照 (`@backup`)
2. 设置 `restore_flag` 标记
3. 执行 `/opt/system_upgrade.sh` 脚本
4. 检测并更新内核 (如果 `/opt/kernel_upgrade/` 存在新内核)
5. 更新 `current_vol` 指向新子卷
6. 创建新子卷快照
7. 自动重启

### 版本回滚

```bash
sudo ota_run rollback @V0
```

**参数**: `@V0` - 要回滚到的目标子卷

**功能**:
- 恢复目标子卷的内核
- 更新 `current_vol` 指向目标子卷
- 自动重启

### 查看所有子卷

```bash
sudo ota_run list
```

**输出示例**:
```
ID 256 gen 100 top level 5 path @V0
ID 257 gen 101 top level 5 path @V1
ID 258 gen 102 top level 5 path @backup
```

### 恢复备份

```bash
sudo ota_run restore
```

**功能**:
- 从 `@backup` 子卷恢复系统
- 删除当前工作子卷并从备份重建
- 恢复备份的内核
- 自动重启

### 删除子卷

```bash
sudo ota_run delete @V1
```

**参数**: `@V1` - 要删除的子卷名称

**限制**:
- 不能删除当前运行的子卷
- 不能删除 `@backup` 子卷

## 自定义升级脚本

升级脚本位于 `/opt/system_upgrade.sh`，需要用户根据需求自定义：

```bash
#!/bin/sh

# Step 1: 等待网络就绪
wait_for_network() {
    echo "[INFO] Waiting for network..."
    while true; do
        ping -c 1 -W 1 8.8.8.8 >/dev/null 2>&1
        if [ $? -eq 0 ]; then
            echo "[INFO] Network is UP"
            break
        fi
        sleep 2
    done
}

# Step 2: 同步时间
sync_time() {
    timedatectl set-ntp true
    sleep 2
}

# Step 3: 执行系统更新
echo "====================================="
echo " Running System Upgrade"
echo "====================================="

wait_for_network
sync_time

echo "[INFO] Running apt update..."
apt update

echo "[INFO] Running apt upgrade..."
apt upgrade -y

# Step 4 (可选): 内核升级
# 将新内核放到 /opt/kernel_upgrade/ 目录:
# /opt/kernel_upgrade/linux-qcm6490-idp.efi
# /opt/kernel_upgrade/linux-qcm6490-idp.efi.md5
# OTA 工具会自动检测并更新内核
```

### 内核升级说明

如需同时升级内核：

1. 将新内核文件放置到 `/opt/kernel_upgrade/linux-qcm6490-idp.efi`
2. 生成 MD5 校验文件: `md5sum linux-qcm6490-idp.efi > linux-qcm6490-idp.efi.md5`
3. OTA 工具会自动检测并更新内核

**注意**: 必须同时提供 `.efi` 和 `.md5` 文件，否则内核升级不会执行

## 自动回滚机制

### restore_flag 机制

```
升级开始
    │
    ▼
设置 restore_flag
    │
    ▼
执行升级脚本
    │
    ├─── 成功 ──→ 删除 restore_flag
    │              升级完成
    │
    └─── 失败 ──→ 重启
                   │
                   ▼
              initramfs 检测到 restore_flag
                   │
                   ▼
              从 @backup 恢复系统
                   │
                   ▼
              恢复完成，系统正常运行
```

### 触发自动回滚的情况

- 升级脚本执行失败 (返回非 0 状态码)
- 升级过程中系统意外重启
- 升级过程中用户中断 (Ctrl+C)

## systemd 服务

OTA 服务通过 systemd 管理：

```bash
# 查看服务状态
systemctl status quectel-ota.service

# 禁用服务 (不推荐)
sudo systemctl disable quectel-ota.service

# 启用服务
sudo systemctl enable quectel-ota.service
```

**服务特性**:
- 类型: `oneshot` (一次性执行)
- 依赖: `local-fs.target`
- 前置条件: `/var/persist/fct_done_flag` 文件存在

## 典型使用场景

### 场景 1: 首次部署

```bash
# 1. 初始化 OTA 功能
sudo ota_run init

# 系统自动重启后...

# 2. 验证当前状态
sudo ota_run current
# 输出: /@V0

# 3. 创建初始备份
sudo ota_run backup
```

### 场景 2: 系统升级

```bash
# 1. 创建备份
sudo ota_run backup

# 2. 执行升级
sudo ota_run upgrade @V1

# 系统自动重启并完成升级...

# 3. 验证升级结果
sudo ota_run current
# 输出: /@V1
```

### 场景 3: 升级失败回滚

```bash
# 升级失败后系统自动从 @backup 恢复
# 或者手动回滚到指定版本

# 回滚到 V0
sudo ota_run rollback @V0
```

### 场景 4: 版本管理

```bash
# 查看所有版本
sudo ota_run list

# 删除旧版本 (不能删除当前版本和备份)
sudo ota_run delete @V0
```

## 注意事项

### 前置条件

1. **工厂初始化**: 系统必须完成工厂初始化 (存在 `/var/persist/fct_done_flag`)
2. **Btrfs 分区**: 根文件系统必须使用 Btrfs 格式
3. **磁盘空间**: 确保有足够空间创建子卷快照

### 安全提示

1. **升级前备份重要数据**: 虽然 OTA 有自动回滚机制，但建议额外备份关键数据
2. **确保电源稳定**: 升级过程中断电可能导致系统损坏
3. **测试内核**: 如需内核升级，务必先测试新内核的兼容性
4. **网络环境**: 升级脚本可能需要网络连接，确保网络稳定

### 限制

1. 子卷名称必须以 `@` 开头，只能包含字母、数字、`.`、`_`、`-`
2. 不能使用 `@backup` 作为工作子卷名称
3. 不能删除当前运行的子卷
4. 不能回滚到 `@backup` 子卷

## 故障排查

### 问题: OTA 工具提示 "System not ready"

**原因**: 工厂初始化未完成

**解决**: 确保 `/var/persist/fct_done_flag` 文件存在

### 问题: 升级后系统无法启动

**原因**: 升级过程中断或内核不兼容

**解决**:
1. 系统会自动从 `@backup` 恢复
2. 如未自动恢复，手动进入恢复模式

### 问题: 内核升级未执行

**原因**: 内核文件或 MD5 文件缺失

**解决**: 确保 `/opt/kernel_upgrade/` 目录下同时存在:
- `linux-qcm6490-idp.efi`
- `linux-qcm6490-idp.efi.md5`

### 问题: 子卷创建失败

**原因**: 磁盘空间不足或 Btrfs 文件系统损坏

**解决**:
1. 检查磁盘空间: `btrfs filesystem usage /`
2. 检查文件系统: `btrfs check /dev/sda3`

## 技术细节

### 文件路径说明

| 文件 | 位置 | 说明 |
|------|------|------|
| ota_run | `/usr/sbin/ota_run` | 主命令行工具 |
| system_upgrade.sh | `/opt/system_upgrade.sh` | 升级脚本模板 |
| current_vol | `/opt/current_vol` | 当前活跃子卷 |
| history_ver | `/opt/history_ver` | 历史版本记录 |
| restore_flag | `/restore_flag` | 恢复模式标记 |
| fct_done_flag | `/var/persist/fct_done_flag` | 工厂初始化标记 |

### 环境变量

OTA 脚本内部使用的变量:

```bash
TOP_DIR=/mnt/top           # Btrfs 顶层挂载点
OTA_INFO_DIR=opt           # 配置目录
WORK_SUBVOL=@V0            # 当前工作子卷 (从 current_vol 读取)
BACKUP_SUBVOL=@backup      # 备份子卷
UPGRADE_SCRIPT=/opt/system_upgrade.sh  # 升级脚本路径
```

---

**作者**: Igni.Li (Quectel)
**许可证**: MIT
**版本**: 1.0
**日期**: 2026
