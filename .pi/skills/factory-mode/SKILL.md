---
name: factory-mode
description: "工厂模式(产测模式)全生命周期: 标记文件机制、进入/退出、写号检测与校验、persist 全擦后的恢复、AT/USB 口恢复"
version: 1.0.0
author: Hermes Agent
license: MIT
platforms: [linux]
metadata:
  hermes:
    tags: [factory, fct, 产测, 写号, persist, atci, mac, nv_data, recovery]
    related_skills: [at-debug, qsm565dwf-sdk, qcs6490-flash]
---

# 工厂模式（产测模式）

**一个 0 字节的标记文件 `/var/persist/fct_done_flag` 决定整机走"产线产测"还是"出货正常"两套行为。
文件不存在 = 工厂模式（默认态）；文件存在 = 正常模式。**

本文所有结论都在 QSM565DWF（QCS6490，Linux 6.6.38）实板上验证过，实测记录见第 7、9 节。

---

## 1. 第一件事：判断现在处于哪种模式

```bash
ls /var/persist/fct_done_flag >/dev/null 2>&1 && echo NORMAL || echo FACTORY
```

四条交叉判据（缺一不可时按顺序看）：

```bash
cat   /sys/module/typec_ucsi/parameters/fct_client   # Y=工厂模式(本次改动引入), N=正常
systemctl is-active atci fan_ctrl                    # 工厂: atci=active fan_ctrl=inactive
ls /dev/ttyGS0                                       # 板上 gadget 串口, 仅工厂模式存在
ls /dev/ttyACM*                                      # 主机侧 AT 口, 仅工厂模式存在
```

### 两种模式的行为差异（实测）

| 组件 | 工厂模式（flag 缺） | 正常模式（flag 在） |
|---|---|---|
| `atci.service` | **active**，AT 口可用 | `ConditionPathExists=!flag` → 根本不启动 |
| USB gadget | `05c6:902d` = RNDIS+DIAG+ADB **+ ACM(AT 口 f4)** | `05c6:902d` 无 ACM |
| `/dev/ttyGS0` / `/dev/ttyACM0` | 有 | 无 |
| `fct_client`（typec_ucsi） | `Y` → Type-C 锁 device | `N` → 走 UCSI 原生 OTG |
| `fct-usb-client.service` | 轮询，往 fct_client 写 1 | 条件早退，不写 sysfs |
| `fan_ctrl.service` | **不启动**（风扇由 `AT+FANON/OFF` 独占） | active，每 5s 按温度写 pwm1 |
| `systemd-logind` | `pi-cleanup.sh` 主动 stop | 保留 |
| `pi` 用户 | 保留（默认密码态） | `pi-cleanup.sh` 删除 |
| `dhcpcd.service` | `ConditionPathExists=flag` → 不启动 | active |
| `quectel-ota.service` | `ConditionPathExists=flag` → 不启动 | active |
| `pi-cleanup.service` | 停 logind + 修/挂 persist | 删 pi 用户 + 修/挂 persist |

> **注意**：`ConditionPathExists` 只在 unit 启动时求值，**没有 path unit 监听**。
> 手动 `rm`/`touch` flag 后，必须重启对应服务（或整机 reboot），否则状态不会变。

---

## 2. 退出工厂模式

### 2.1 标准方式：`AT+QFCT="done"`

AT 口（`/dev/ttyACM0`，115200 8N1）发：

```
AT+QFCT="done"
```

源码 `ql_at_factory.c` 里 handler 实际做的事（按顺序）：

1. `systemctl stop gdm.service`
2. 删 `/etc/gdm3/daemon.conf`、`/etc/gdm3/custom.conf`
3. `pkill -9 -u pi && userdel -r pi`，删 `/home/pi`
4. `mkdir -p /opt/persist_backup`
5. **`touch /var/persist/fct_done_flag`** ← 关键
6. `cp -a /var/persist/* /opt/persist_backup/` ← 建立 rootfs 侧备份（写号的保命备份）
7. `sync`

**副作用**：`pi` 用户被删；出厂默认密码失效；`gdm` 配置被删。
**不会做**：不 `systemctl start fan_ctrl`（源码注释说会，实际没有）——重启后由
`ConditionPathExists=flag` 自然拉起。

### 2.2 退出后必须验证

```bash
systemctl is-active atci        # inactive
ls /dev/ttyACM*                 # 不存在
cat /sys/module/typec_ucsi/parameters/fct_client   # N
ls /opt/persist_backup/         # 有 NV_data / mac_addr / wlan_mac.bin / fct_done_flag
```

**建议退出后 reboot 一次**：`atci` 已启动的服务不会因 flag 出现而自动退出（实测
`AT+QFCT="done"` 后 `atci` 仍是 `active`，直到重启才变 `inactive`）。

---

## 3. 重新进入工厂模式（返修 / 产线返工）

### 3.1 方式 A：重启（推荐，最稳）

```bash
adb shell rm -f /var/persist/fct_done_flag
adb shell reboot          # QCS6490 必须 adb shell reboot, adb reboot 无效
```

### 3.2 方式 B：不重启（实测可用，约 15 秒）

```bash
adb shell 'rm -f /var/persist/fct_done_flag;
           systemctl start atci;
           systemctl restart fct-usb-client;
           nohup qusb init >/tmp/qusb.log 2>&1 &'
```

`qusb init` 会重建 gadget 配置，把 `acm.usb0` 加回来 → 板上出现 `/dev/ttyGS0`，
主机侧重新枚举 `05c6:902d` 并出现 `/dev/ttyACM0`。

实测：`adb` 会断开约 3~5 秒后自动回来（`remove_configs` 只删 `configs/c.1/f*`
符号链接，不销毁 function 实例，adbd 的 ffs fd 不受影响）。
⚠️ 期间别断电、别在 `qusb init` 跑完前拔线。

### 3.3 顺带：Type-C 锁 device

`fct_client` 由 `fct-usb-client.service` 维护：
- flag 缺失 → `echo 1 > /sys/module/typec_ucsi/parameters/fct_client`，Type-C 恒为 device
- flag 出现 → 轮询发现后把参数拨回 `0` 并退出（`fct_done_flag appeared, released`）

只要服务 `enabled`，重启后自动生效，不用手工操作。

---

## 4. 写号：一共写 4 样东西

| 号 | AT 命令（写入） | 回读 | 落地位置 |
|---|---|---|---|
| **SN / 设备序号** | `AT+EGMR=1,5,"P1Y26I2270002760P"` | `AT+EGMR=0,5` | `NV_data.sn` |
| **WiFi MAC** | `AT+QNVW=4678,0,"DC2E97AF4D75"` | `AT+QNVR=4678,0` | `NV_data.wifi_mac` + `/var/persist/wlan_mac.bin` |
| **BT MAC** | `AT+QNVW=447,0,"484CAF972EDC"` | `AT+QNVR=447,0` | `NV_data.bt_mac` → 开机生成 `/etc/bluetooth/.bt_nv.bin` |
| **网口 MAC** | `AT+QMAC="MACW",0,DC2E97AF4EA2` | `AT+QMAC?` 或 `AT+QMAC="MACR",0` | `/var/persist/mac_addr` |

### 格式陷阱（踩过）

| 指令 | 要求 | 违例后果 |
|---|---|---|
| `AT+QNVW` | 参数**必须正好 12 位十六进制**，无分隔符 | 长度≠12 → 直接 `ERROR` |
| `AT+EGMR` | 字符串，**不校验长度**（实瘃 17 位 `P1Y26...` 可用）；但 `sn` 字段只有 128 字节，命令里的串 **超过 127 字符会溢出到 `wifi_mac`/`bt_mac`** | 溢出不报错，会静默搞坏后面的 MAC 字段 |
| `AT+QMAC` | 12 位无冒号，写文件时自动插冒号成 `xx:xx:...` | 长度≠12 → `ERROR` |
| 回读 `AT+QNVW` | **不存在**，只能 `AT+QNVR` | — |

> `AT+EGMR` 的溢出风险是真实存在的：handler 里 `strncpy(qwifi_data->sn, command, strlen(command))`
> 没有任何上限检查。SN 请控在 20 位以内。

写完整一套参考（4 条，顺序无所谓）：

```
AT+EGMR=1,5,"P1Y26I2270002760P"
AT+QNVW=4678,0,"DC2E97AF4D75"
AT+QNVW=447,0,"484CAF972EDC"
AT+QMAC="MACW",0,DC2E97AF4EA2
```

> `AT+QMAC` 单独存在的原因：`mac_addr` 是**网口 MAC**，跟 WiFi/BT 无关；
> 它同时是第 7 节自动备份的哨兵文件。不写的话开机时
> `check_mac_addr` 会把 eth0 当前 MAC 自学习落盘（每块板/每次重刷可能不同）。

---

## 5. `/var/persist/NV_data` 文件结构（624 字节二进制）

`struct Quec_wifi_fsg_data`（`ql_common_api.h`），按偏移直接解析：

| 字段 | 偏移 | 长度 | 现网实例值 |
|---|---|---|---|
| `magic_head` | 0 | 16 | `Quec-Head-nv1-v1` |
| `enable_dump` | 16 | 16 | 空 |
| `mp_version` | 32 | 32 | 空 |
| `bp_version` | 64 | 32 | 空 |
| `sn` | **96** | 128 | `P1Y26I2270002760P` |
| `wifi_mac` | **224** | 16 | `DC2E97AF4D75` |
| `bt_mac` | **240** | 16 | `484CAF972EDC` |
| `qcsn` | 256 | 128 | **必须留空** |
| `nv_data` | 384 | 128 | **必须留空** |
| `magic_tail` | **512** | 16 | `Quec-Tail-nv1-v1` |
| `ati_info` | 528 | 32 | 空 |
| `key_data` | 560 | 64 | 空 |

### ⚠️ 两个必须知道的陷阱

**陷阱 1：`check_mac_addr` 用"倒数 12 字符"当 BT MAC**

`/usr/sbin/check_mac_addr` 里：

```bash
RAW_LINE=$(grep -aEo "Quec-Head-nv1-v1.*Quec-Tail-nv1-v1" "$NVDATA_FILE" | tr -d '\0\n\r ')
MAC_START=$(( ${#RAW_LINE} - ${#TAIL} - 12 ))
MAC=${RAW_LINE:$MAC_START:12}
```

它把 magic 之间内容去掉 NUL 后，取 **`magic_tail` 前 12 个字符** 当 BT MAC。
这在 `qcsn` 和 `nv_data` 为空时恰好等于 `bt_mac`。
**一旦有人往 `qcsn` / `nv_data` 写数据，BT MAC 就会被解析成 nv_data 的尾巴。**
→ 这两个字段不要手写，只用 `AT+QNVW` 走 `bt_mac` 字段。
→ MAC 解析失败/全 0 时脚本会 fallback 到硬编码 `DEFAULT_MAC=005533225511`。

**陷阱 2：空壳 NV_data 的 magic 是合法的**

`fct_data_init()`（atci 启动时跑）逻辑：

- 读 `NV_data`，magic 头/尾匹配 → 用它
- 否则读 `NV_data_backup`（`Quec-Head-nv2-v1` 那套），匹配 → 用它
- 都不匹配 → 新建空壳：`sn="11111"`、`wifi_mac="000000000000"`、`bt_mac="000000000000"`，
  **写回时 magic 头/尾是完整的 `Quec-Head-nv1-v1` / `Quec-Tail-nv1-v1`**

→ **`ls /var/persist/NV_data` 存在 ≠ 已写号。** 必须看字段值（第 6 节）。

---

## 6. 判断"是否已写号"（三种判据）

### 判据 A：工厂模式下用 AT（最直接）

```
AT+EGMR=0,5      → +EGMR:"11111"              ← 未写号（空壳默认值）
                 → +EGMR:"P1Y26I2270002760P"  ← 已写
AT+QNVR=4678,0   → +QNVR:"000000000000"       ← 未写
                 → +QNVR:"DC2E97AF4D75"       ← 已写
AT+QNVR=447,0    → +QNVR:"000000000000"       ← 未写
                 → +QNVR:"484CAF972EDC"       ← 已写
AT+QMAC="MACR",0 → MACR: "..."                ← 已写（12 位十六进制）
                 → ERROR                        ← mac_addr 文件不存在 = 未写
```

判定规则：**SN 为 `11111`、或 WiFi/BT MAC 为 `000000000000`（12 个 0）→ 未写号。**

### 判据 B：正常模式下直接解析文件（无 AT 口）

`/var/persist/NV_data` 一直挂在那里（`/dev/sda2` → `/var/persist`），adb 就能读：

```bash
f=/var/persist/NV_data
echo -n "head="; dd if=$f bs=1 count=16            2>/dev/null | tr -d '\0'; echo
echo -n "sn  ="; dd if=$f bs=1 skip=96  count=128   2>/dev/null | tr -d '\0'; echo
echo -n "wifi="; dd if=$f bs=1 skip=224 count=16    2>/dev/null | tr -d '\0'; echo
echo -n "bt  ="; dd if=$f bs=1 skip=240 count=16    2>/dev/null | tr -d '\0'; echo
echo -n "tail="; dd if=$f bs=1 skip=512 count=16    2>/dev/null | tr -d '\0'; echo
```

实测输出（已写号的板子）：

```
head=Quec-Head-nv1-v1
sn  =P1Y26I2270002760P
wifi=DC2E97AF4D75
bt  =484CAF972EDC
tail=Quec-Tail-nv1-v1
```

判据：`head`/`tail` 必须完全匹配；`sn` 不能是 `11111`；`wifi`/`bt` 不能是 `000000000000`。

### 判据 C：看落地产物（交叉验证，能发现"号写了但没生效"）

```bash
cat /var/persist/wlan_mac.bin          # Intf0MacAddress=DC2E97AF4D75 / END
cat /lib/firmware/wlan/wlan_mac.bin    # 应与上面一致（开机 check_mac_addr 拷过去）
od -An -tx1 /etc/bluetooth/.bt_nv.bin  # 9 字节: 00 00 06 + BT MAC 逆序
cat /var/persist/mac_addr              # DC:2E:97:AF:4E:A2  (18 字节)
```

实测对照：BT MAC `484CAF972EDC` → `.bt_nv.bin` = `00 00 06 dc 2e 97 af 4c 48`
（即 `48 4C AF 97 2E DC` 字节逆序写入）。

> `wlan_mac.bin` 只由 `AT+QNVW=4678` 写；改完号若 `wlan_mac.bin` 没更新，说明
> 命令失败或写到了 `NV_data` 但没 `sync`。`.bt_nv.bin` 由开机
> `wlan-mac-check.service` 从 `NV_data` 重新生成，**改完 BT MAC 要重启才生效**。

---

## 7. 意外全擦 / "没检测到写号文件" 怎么恢复

### 第 0 步：先分清擦掉了什么

| 现象 | 擦除范围 | 恢复方式 |
|---|---|---|
| adb 进得去，`/var/persist` 里只剩 `lost+found`/目录骨架 | 只擦了 persist 文件 | **自动恢复**（7.1） |
| adb 进得去，`/opt/persist_backup` 也没了 | rootfs 重刷过 | **手工补号**（7.2） |
| persist 挂不上 | 文件系统损坏 | `pi-cleanup.sh` 会 mkfs + 自动恢复（7.1） |
| 整机新板、首刷 | 全空 | 走完整产测流程（写号 → `AT+QFCT="done"`） |

判断备份还在不在：

```bash
ls -l /opt/persist_backup/     # 有 NV_data / mac_addr / wlan_mac.bin → 有救
```

### 7.1 自动恢复：`pi-cleanup.sh` 的 persist 双向备份

`/usr/sbin/pi-cleanup.sh` 里的 `sync_persist_backup()`：

```
哨兵文件 = mac_addr
  persist/mac_addr 在, backup/mac_addr 不在  →  cp -af /var/persist/*  /opt/persist_backup/
  backup/mac_addr  在, persist/mac_addr 不在 →  cp -af /opt/persist_backup/*  /var/persist/
```

挂载失败的处理链（`repair_and_mount_persist()`）：

```
mount /dev/sda2 /var/persist
  ├─ 成功 → sync_persist_backup
  └─ 失败 → blkid 查 FSTYPE
        ├─ 不是 ext4      → mkfs.ext4 -F -L persist   → 重挂 → sync
        └─ 是 ext4        → fsck.ext4 -y              → 重挂 → sync
```

**实测（本次）**：

```bash
rm -f /var/persist/{mac_addr,NV_data,wlan_mac.bin}
/usr/sbin/pi-cleanup.sh
```

输出：

```
[persist] already mounted
[persist] checking bidirectional backup...
[persist] backup exists, persist missing -> restoring to persist
[persist] sync done
```

结果：`NV_data` md5 = `86f3cd82256974b248789c7fdb2ecb2e`，与擦除前**完全一致**；
`fct_done_flag`、`mac_addr`、`wlan_mac.bin` 一并回来，模式仍是正常模式。

**这就是"刷机刷掉了号"最常见的真实情况——其实没丢，只是 persist 被擦了，开机会自动长回来。**

⚠️ 前提：**备份目录是在 `AT+QFCT="done"` 时创建的**。
只写了号、还没退出工厂模式的板子 **没有 `/opt/persist_backup`**，擦了就真没了。
所以产线流程必须走完 `AT+QFCT="done"`。

⚠️ 反向风险（**只在整擦时才成立**）：哨兵文件是 `mac_addr`，不是 flag。
- **只删 `fct_done_flag`** → 两侧 `mac_addr` 都在 → `sync_persist_backup()` 不动作，
  flag 不会被复活（实测：删 flag 后跑 `pi-cleanup.sh` 输出 `sync done`，flag 仍 absent）。
  所以"进工厂模式只删 flag"是安全的。
- **整擦 persist（含 `mac_addr`）** → 触发 `backup → persist` 整目录拷贝 →
  备份里的 `fct_done_flag` 也会被一起拷回来，板子又回到正常模式。
  这种场景下若想留在工厂模式，必须同时删 `/opt/persist_backup/fct_done_flag`
  或直接 `rm -rf /opt/persist_backup`。

### 7.2 手工补号（备份也没了）

按顺序做，每步都有验证点：

```bash
# 1) 进工厂模式（AT 口要回来）
adb shell rm -f /var/persist/fct_done_flag
# 防止整擦恢复时把旧 flag 拷回。只删 flag 即可，千万别整个删备份目录：
# /opt/persist_backup/NV_data 里可能还存着老号（先 `adb pull` 抄下来再动）
adb shell rm -f /opt/persist_backup/fct_done_flag
adb shell reboot

# 2) 确认 AT 口
ls /dev/ttyACM0                              # 主机侧

# 3) 写回 4 个号（值来自产线记录/MES/铭牌）
AT+EGMR=1,5,"<SN>"
AT+QNVW=4678,0,"<12位WiFi MAC>"
AT+QNVW=447,0,"<12位BT MAC>"
AT+QMAC="MACW",0,<12位网口MAC>

# 4) 立即回读验证
AT+EGMR=0,5
AT+QNVR=4678,0
AT+QNVR=447,0
AT+QMAC="MACR",0

# 5) 退出工厂模式（同时建立 /opt/persist_backup 备份）
AT+QFCT="done"

# 6) 重启，让 check_mac_addr 重建 .bt_nv.bin / 拷 wlan_mac.bin
adb shell reboot

# 7) 终检
ls /opt/persist_backup/                       # NV_data 等在
od -An -tx1 /etc/bluetooth/.bt_nv.bin         # BT MAC 正确
cat /lib/firmware/wlan/wlan_mac.bin           # WiFi MAC 正确
systemctl is-active atci                      # inactive
```

**实测结论**：全擦后按上面写回同样的值，`NV_data` 与擦除前
**md5 逐字节一致**（`86f3cd82256974b248789c7fdb2ecb2e`，`cmp -l` 零差异）。

⚠️ **`mac_addr` 不会被自动补回**：它只能靠 `AT+QMAC="MACW",0,...` 显式写，
或靠开机时 `check_mac_addr` 自学习 eth0 当前 MAC。重刷后 eth0 MAC 可能变，
要固定就得显式写。另外 `AT+QNVW` **不会**更新 `mac_addr`，别把 WiFi MAC 当网口 MAC。

---

## 8. persist 分区事实（哪些操作会丢号）

```bash
/dev/sda2  LABEL=persist  ext4  →  /var/persist
fstab: /dev/disk/by-partlabel/persist  /var/persist  ext4  defaults,nofail,noatime  0 0
```

| 操作 | 会丢 persist？ | 说明 |
|---|---|---|
| `buildall` + `flash.sh` 烧录 | **不会** | `flash.sh` 显式 `grep -v WIPE_PARTITIONS \| grep -v BLANK_GPT`；`rawprogram0.xml` 里 persist 条目 `filename=""` |
| QFIL/QDL 手动勾全擦 / 附加 `WIPE_PARTITIONS.xml` | **会** | 固件目录里默认没有 `*WIPE*.xml`，别自己加 |
| 重刷 GPT（`gpt_main*.bin`/`gpt_backup*.bin`） | **可能** | 分区表变大会重新布局 |
| `mkfs` / `dd` / `fdisk` persist | **会** | 手工操作 |
| persist 文件系统损坏 | **会（但自动恢复）** | `pi-cleanup.sh` mkfs 后从 `/opt/persist_backup` 拷回 |
| 换板 / 换模组 | — | 号在新板上不存在，必须重写 |

**结论**：常规刷机不会掉号。报"号没了"的板子，九成是
① 刷了带 WIPE 的包 ② 手动格式化过 persist ③ 是另一块新板 ④ `pi-cleanup` 把备份覆盖错了。

---

## 9. 踩过的坑速查

1. **`ls NV_data` 不能当写号判据** —— 空壳文件的 magic 合法，`sn=11111`。
2. **`qcsn`/`nv_data` 字段不能手写** —— `check_mac_addr` 靠"倒数 12 字符"解析 BT MAC。
3. **删 flag 是安全的，整擦 persist 不是** —— 自动恢复的哨兵是 `mac_addr`；只删 flag 不会
   被复活，但整擦 persist 会把备份里的 flag 一起拷回来（回到正常模式）。
4. **`ConditionPathExists` 不自动重估** —— 改 flag 后要重启服务或 reboot。
5. **`AT+QFCT="done"` 后 atci 不会自己停** —— 服务已启动，condition 不再求值。
6. **AT 口刚出现时会被 ModemManager 抢几百 ms** —— 主机侧脚本打开 `/dev/ttyACM0` 要重试，
   否则 `OSError: [Errno 16] Device or resource busy`。
7. **`AT+QNVW` 严格 12 位** —— 带冒号、带 `0x`、13 位都 `ERROR`，且不回具体原因。
8. **`AT+QNVW` 没有回读形式** —— 只能 `AT+QNVR=<nv>,0`。
9. **改完 BT MAC 要重启** —— `.bt_nv.bin` 由 `wlan-mac-check.service` 从 `NV_data` 生成。
10. **`AT+QFCT="done"` 会删 `pi` 用户** —— 之后 adb shell 是 root，桌面登录用户没了。
11. **不重启切工厂模式** 时用 `qusb init`，`adb` 会断 3~5 秒（实测自己回来），别在跑一半时操作。
12. **`AT+QUSBMODE`** 报 `3`=super-speed / `2`=high-speed，用来确认上位机走的是 USB3 还是 USB2。

---

## 10. 命令卡片

```bash
# ── 模式判定 ──
adb shell 'ls /var/persist/fct_done_flag >/dev/null 2>&1 && echo NORMAL || echo FACTORY'

# ── 退出工厂模式 ──
python3 quectel_build/tools/at.py 'AT+QFCT="done"'
adb shell reboot

# ── 进入工厂模式 ──
adb shell rm -f /var/persist/fct_done_flag
adb shell 'rm -f /opt/persist_backup/fct_done_flag'
adb shell reboot

# ── 写号 ──
python3 quectel_build/tools/at.py \
  'AT+EGMR=1,5,"<SN>"' \
  'AT+QNVW=4678,0,"<WiFiMAC>"' \
  'AT+QNVW=447,0,"<BTMAC>"' \
  'AT+QMAC="MACW",0,<EthMAC>'

# ── 读号（AT）──
python3 quectel_build/tools/at.py 'AT+EGMR=0,5' 'AT+QNVR=4678,0' 'AT+QNVR=447,0' 'AT+QMAC="MACR",0'

# ── 读号（无 AT，直接解析 NV_data）──
adb shell 'f=/var/persist/NV_data; for o in 0:16 96:128 224:16 240:16 512:16; do \
  s=${o%:*}; c=${o#*:}; dd if=$f bs=1 skip=$s count=$c 2>/dev/null | tr -d "\0"; echo; done'

# ── 自动恢复 persist ──
adb shell 'rm -f /var/persist/mac_addr; /usr/sbin/pi-cleanup.sh 2>&1 | grep persist'
```

---

## 11. 源码 / 文件索引

| 内容 | 路径 |
|---|---|
| `fct_done_flag` 创建（`AT+QFCT="done"`） | `layers/meta-quectel/recipes-quectel/atcid/files/atci/quectel/src/ql_at_factory.c` |
| `AT+EGMR` / `AT+QNVW` / `AT+QNVR` / `AT+QMAC` / `AT+QFCT` / `AT+QUSBMODE` handler | 同上（`QL_AT_*_Handle`） |
| 命令注册表 | `.../atci/quectel/inc/ql_atcmd_info.h` |
| `NV_data` 结构与读写 | `.../atci/quectel/src/ql_common_api.c`、`inc/ql_common_api.h` |
| `atci.service` condition | `.../atci/scripts/atci.service` |
| persist 修复/备份 | `.../atci/scripts/pi-cleanup.sh` |
| 风扇温控 condition | `.../atci/scripts/fan_ctrl.service` |
| Type-C 锁 device | `.../atci/scripts/fct_usb_client.sh` + `.../atci/scripts/fct-usb-client.service` + 内核 `drivers/usb/typec/ucsi/ucsi.c` |
| USB gadget 组合（ACM 有无） | `layers/meta-quectel/recipes-bsp/usb/files/qusb`（`usb_init` / `run_902D`） |
| MAC 落地/自学习 | `/usr/sbin/check_mac_addr` + `wlan-mac-check.service`（`wifi-bt-drv` recipe） |
| 烧录排除 WIPE | `quectel_build/tools/flash.sh` |

---

## 12. 本 skill 的实测环境

- 板卡：QSM565DWF / QCS6490，固件 `QSM565DWFPARL1A01_BP01.001_Linux6.6.38_V01`（LINUX STD）
- adb serial `44afa12f`，`/dev/sda2` persist ext4
- 本次验证覆盖：模式双向切换（含不重启路径）、`AT+QFCT="done"` 全副作用、4 条写号命令
  + 3 条回读、`NV_data` 字节级还原、`pi-cleanup` 自动恢复、`.bt_nv.bin`/`wlan_mac.bin` 落地
- **未覆盖**：QFIL 全擦包实测、GPT 重刷影响、`NV_data_backup`（`nv2` 那套）的实际产生路径
