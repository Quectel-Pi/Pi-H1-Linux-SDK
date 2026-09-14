# YT6801 RJ45 LED 调试记录（Quectel PI H1 / QCS6490）

> 板卡：Quectel PI H1 (QCS6490)
> 驱动：`sources/quectel-src/ethernet-drivers/yt6801`（裕太微 YT6801SH，v1.0.27）
> 原理图：`quectel_build/doc/Quectel-Pi-H1_SCH_V1.3_20251128.pdf` 第 12 页
> 日期：2026-08-05

---

## 1. 问题现象

RJ45 网口指示灯完全不亮。网线插好、千兆协商成功（`ethtool` 显示 1000Mb/s, Link detected: yes），
但黄灯、绿灯均不亮。

---

## 2. 硬件信息（原理图确认）

### 2.1 芯片与 LED 引脚

YT6801SH（U1301，QFN32，PCIe 千兆网卡）提供 3 路可编程 LED 引脚：

| 芯片引脚 | 信号名 | 原理图电阻 | RJ45 灯 |
|---------|--------|-----------|---------|
| LED0 (PIN28) | RTL&YT_LED0 | R1351 330R（已贴） | GREEN 绿灯 |
| LED1 (PIN29) | RTL&YT_LED1 | R1326 330R | YELLOW（板上实际未接灯） |
| LED2 (PIN30) | RTL&YT_LED2 | R1350 330R | YELLOW 黄灯 |

> 注意：R1325（NM-470R）是连到 ETH33 的备用支路，不贴片，不影响绿灯通路。
> 板上 RJ45 实际只有**一绿一黄**两个可见灯：绿灯由 LED0 驱动，黄灯由 LED2 驱动，
> **LED1 引脚未连接到任何可见灯**。

### 2.2 LED 极性

灯阴极经电阻接地，芯片输出电平点亮（拉电流）。实测确认芯片 LED 引脚极性正常，
0x07/0x1800 等值能点亮对应灯。

---

## 3. 根因分析

### 3.1 LED 配置机制（驱动代码）

YT6801 的 LED 行为由 **PHY 扩展寄存器**控制（经 MDIO 0x1E/0x1F 间接访问）：

| 寄存器 | 地址 | 说明 |
|-------|------|------|
| LED_CFG（公共） | 0xA00B | 公共配置（实测默认 0xe000） |
| LED0_CFG | 0xA00C | 绿灯配置 |
| LED1_CFG | 0xA00D | 黄灯（未接）配置 |
| LED2_CFG | 0xA00E | 黄灯配置 |
| LED_BLINK | 0xA00F | 闪烁配置 |

驱动启动时从 **eFuse Byte0（EFUSE_LED_ADDR=0x00）** 读 5bit LED 方案索引
（`EFUSE_LED_POS=0, EFUSE_LED_LEN=5`），然后：

- `0x1F` → COMMON_SOLUTION：用 eFuse 里存的 20 个寄存器值配置
- `0~4` → 用硬编码 SOLUTION0~4 表
- **其他值（无效）→ fallback 到 SOLUTION0**

对应代码：`fuxi-gmac-hw.c` → `fxgmac_release_phy()`（约 4415 行起）。

### 3.2 板上实测数据

用自写 ioctl 工具读取 eFuse：

```
eFuse[0x00] = 0xaa  -> LED solution index = 10 (0x0A)
```

**0x0A 不在 0~4 也不等于 0x1F → 无效值 → 驱动落入 SOLUTION0 分支**。

SOLUTION0 写入值（`fuxi-gmac-reg.h`）：

| 寄存器 | SOLUTION0 值 | 后果 |
|-------|-------------|------|
| LED0_CFG | 0x2600 | 非千兆编码，**千兆下不亮** |
| LED1_CFG | 0x1800 | 板上未接灯，无影响 |
| LED2_CFG | 0x00 | **黄灯被禁用** |

→ 所以千兆下：绿灯（0x2600 不匹配千兆）+ 黄灯（0x00 禁用）= 全灭。

### 3.3 寄存器编码实测结论（本板硬件验证）

在硬件上逐个写值、肉眼观察灯，最终确认编码语义：

| 值 | 行为 | 语义 |
|----|------|------|
| 0x07 | 全速率常亮+闪烁 | bit0=链路常亮, bit1=活动闪, bit2=基础位(必须) |
| 0x06 | 有流量才闪，无流量灭 | 纯活动闪烁（bit1+bit2，无常亮） |
| **0x1800** | **千兆下常亮** | **千兆匹配编码** |
| **0x2600** | **非千兆(百兆/10M)下常亮** | **非千兆匹配编码** |
| 0x00 | 灭 | 关闭 |

关键调试过程：
1. eFuse=0x0A 无效 → 原 SOLUTION0 全灭
2. 重映射到 SOLUTION2（0x20/0x40/0x07）→ 黄灯闪（LED2 被点亮），绿灯不亮（0x20 非千兆）
3. 试 0x86/0x46/0x06 等低字节值 → 都能亮（bit0=1 时速率位被忽略）
4. 试官方高字节值 → **0x1800 千兆亮、0x2600 千兆灭**
5. 交叉验证：LED0=0x1800 千兆绿灯常亮 + LED2=0x2600 黄灯灭 → 编码锁定

---

## 4. 代码修改

### 4.1 修复文件清单

| 文件 | 改动 |
|------|------|
| `fuxi-gmac-hw.c` | +71 行：修复无效 eFuse 值 fallback + 新增按速率配置函数 |
| `fuxi-gmac-net.c` | +3 行：link up 时调用 `led_under_speed` |
| `fuxi-gmac.h` | +1 行：hw_ops 新增 `led_under_speed` 声明 |

### 4.2 修改点 1：修复无效 eFuse 方案值（fuxi-gmac-hw.c ~4425 行）

eFuse 方案值 > SOLUTION4（无效）时，不再走 SOLUTION0，直接写本板验证过的编码：

```c
if (EFUSE_LED_COMMON_SOLUTION != value) {
    /*
     * Quectel PI H1: eFuse LED solution index = 0x0A (invalid)
     * Board-verified: 0x1800 = gigabit, 0x2600 = non-gigabit
     */
    fxgmac_write_ephy_reg(pdata, REG_MII_EXT_ADDR,
                          REG_MII_EXT_COMMON_LED0_CFG);
    fxgmac_write_ephy_reg(pdata, REG_MII_EXT_DATA, 0x1800);   /* 绿灯: 千兆 */
    fxgmac_write_ephy_reg(pdata, REG_MII_EXT_ADDR,
                          REG_MII_EXT_COMMON_LED1_CFG);
    fxgmac_write_ephy_reg(pdata, REG_MII_EXT_DATA, 0x00);     /* LED1 未接 */
    fxgmac_write_ephy_reg(pdata, REG_MII_EXT_ADDR,
                          REG_MII_EXT_COMMON_LED2_CFG);
    fxgmac_write_ephy_reg(pdata, REG_MII_EXT_DATA, 0x2600);   /* 黄灯: 非千兆 */
    goto led_done;
    ...原 SOLUTION switch 保留（有效 eFuse 方案值仍走原逻辑）...
led_done:
    }
```

> 有效方案值（0~4 / 0x1F）仍走原逻辑，不影响其他板型。

### 4.3 修改点 2：按速率动态配置 LED（fuxi-gmac-hw.c ~4645 行）

新增 `fxgmac_config_led_under_speed(pdata, speed)`：

```c
#define FXGMAC_LED_VAL_GIGA     0x1800   /* 千兆匹配, 常亮 */
#define FXGMAC_LED_VAL_OTHER    0x2600   /* 非千兆(百兆/10M)匹配, 常亮 */
#define FXGMAC_LED_VAL_ACTIVITY 0x06     /* 活动闪烁: bit1+bit2, 无常亮 */

static void fxgmac_config_led_under_speed(struct fxgmac_pdata *pdata, int speed)
{
    u32 green, yellow;

    if (speed == SPEED_1000) {
        /* 千兆: 黄灯常亮 + 绿灯随数据闪 */
        green  = FXGMAC_LED_VAL_ACTIVITY;
        yellow = FXGMAC_LED_VAL_GIGA;
    } else {
        /* 百兆/10M: 绿灯常亮 + 黄灯随数据闪 */
        green  = FXGMAC_LED_VAL_OTHER;
        yellow = FXGMAC_LED_VAL_ACTIVITY;
    }

    fxgmac_write_ephy_reg(pdata, REG_MII_EXT_ADDR, REG_MII_EXT_COMMON_LED0_CFG);
    fxgmac_write_ephy_reg(pdata, REG_MII_EXT_DATA, green);
    fxgmac_write_ephy_reg(pdata, REG_MII_EXT_ADDR, REG_MII_EXT_COMMON_LED1_CFG);
    fxgmac_write_ephy_reg(pdata, REG_MII_EXT_DATA, 0x00);
    fxgmac_write_ephy_reg(pdata, REG_MII_EXT_ADDR, REG_MII_EXT_COMMON_LED2_CFG);
    fxgmac_write_ephy_reg(pdata, REG_MII_EXT_DATA, yellow);

    DPRINTK("fxgmac led under speed %d: led0=0x%04x led2=0x%04x\n",
            speed, green, yellow);
}
```

hw_ops 注册（~6272 行）：

```c
hw_ops->led_under_active = fxmgac_config_led_under_active;
hw_ops->led_under_speed  = fxgmac_config_led_under_speed;   /* 新增 */
```

### 4.4 修改点 3：link up 时触发（fuxi-gmac-net.c ~375 行）

在 `fxgmac_phy_process()` 中，link up 且拿到 `phy_speed` 后调用：

```c
hw_ops->config_mac_speed(pdata);

/* 按协商速率配置 RJ45 双色 LED (千兆黄/百兆绿) */
hw_ops->led_under_speed(pdata, pdata->phy_speed);

hw_ops->enable_rx(pdata);
```

---

## 5. 最终行为

| 协商速率 | 绿灯 (LED0) | 黄灯 (LED2) |
|---------|------------|------------|
| 1000M | 随数据闪烁 | 常亮 |
| 100M / 10M | 常亮 | 随数据闪烁 |
| 断开 | 灭 | 灭 |

---

## 6. 编译与验证

### 6.1 交叉编译 .ko（无需全量烧录）

```bash
# 主机交叉编译（模拟 bitbake 构建路径）
ROOT=/home/igni/Downloads/debian/1.7-QuecPi-QCLinux-BL01
SRC="$ROOT/sources/quectel-src/ethernet-drivers/yt6801"
KBUILD="$ROOT/build-qcom-wayland/tmp-glibc/work/qcm6490_idp-qcom-linux/linux-qcom-custom/6.6/build"
CROSS_PREFIX="$ROOT/build-qcom-wayland/tmp-glibc/work/qcm6490-qcom-linux/qcom-gen-partition-bins/1.0/recipe-sysroot-native/usr/bin/aarch64-qcom-linux/aarch64-qcom-linux-"

WORK=$(mktemp -d /tmp/yt6801-build.XXXXXX)
cp -r "$SRC"/. "$WORK/"
cd "$WORK"
make ARCH=arm64 CROSS_COMPILE="$CROSS_PREFIX" -C "$KBUILD" M="$WORK" modules
# 产物: $WORK/yt6801.ko
```

### 6.2 adb 替换 + 热加载

```bash
ADB=./quectel_build/tools/adb
MOD=/lib/modules/$(adb shell uname -r)/updates/yt6801.ko

adb push yt6801.ko /tmp/yt6801.ko.new
adb shell "cp $MOD /tmp/yt6801.ko.orig"          # 备份
adb shell "cp /tmp/yt6801.ko.new $MOD"            # 替换
adb shell "rmmod yt6801 && modprobe yt6801"       # 热加载
adb shell "ethtool eth0 | grep -E 'Speed|Link detected'"
```

### 6.3 验证结果（实测）

```
# dmesg 确认动态配置触发
fxgmac led under speed 1000: led0=0x0006 led2=0x1800

# 寄存器实测
LED_CFG   [0xA00B] = 0xe000
LED0_CFG  [0xA00C] = 0x0006   (绿灯: 千兆活动闪)
LED1_CFG  [0xA00D] = 0x0000   (未接)
LED2_CFG  [0xA00E] = 0x1800   (黄灯: 千兆常亮)
LED_BLINK [0xA00F] = 0x0006

# 肉眼观察（千兆）
黄灯常亮 + 绿灯随数据闪烁  ✅
```

---

## 7. 运行时调试工具（ioctl）

驱动 debugfs 暴露了 PHY 寄存器读写 ioctl（`FXGMAC_SET_PHY_REG` 0x10018 /
`FXGMAC_GET_PHY_REG` 0x10017），可在运行时直接改 LED 寄存器，无需反复编译：

```c
/* 写 LED 扩展寄存器 = 先写地址 0x1E 再写数据 0x1F */
phy_write(0x1E, 0xA00C);   /* 选 LED0_CFG */
phy_write(0x1F, 0x1800);   /* 写值 */

/* 读 = 写地址后读数据 */
phy_write(0x1E, 0xA00C);
phy_read(0x1F, &val);
```

ioctl 结构（与内核 `fuxi-os.h` 一致）：

```c
struct ext_ioctl_data {
    unsigned int cmd_type;
    struct ext_command_buf cmd_buf;  /* { void *buf; u32 size_in; u32 size_out; } */
};
/* CMD_DATA: { u32 val0; u32 val1; u32 val2; }
 * SET_PHY_REG: val0=reg_id, val1=data
 * GET_PHY_REG: val0=reg_id, 返回 val1 */
```

---

## 8. 遗留事项 / 注意

1. **百兆场景未实测**：动态逻辑已编译通过，但只在千兆下验证过。百兆口实测预期：
   绿灯常亮 + 黄灯闪。若行为不符，检查 `fxgmac_phy_process()` 中 `cur_speed` 解码
   （`cur_speed==2 → SPEED_1000, ==1 → SPEED_100, else SPEED_10`）。
2. **当前修改对无效 eFuse 方案值（>4 且 !=0x1F）生效**；有效方案值走原逻辑，不影响其他板型。
3. **0x1800/0x2600 为高字节编码，自带"无闪烁"**；活动闪烁用低字节 0x06（bit1+bit2）。
4. 若后续需要精确 bit 级定义（如速率选择位、极性位），需向裕太微索取 YT6801 datasheet
   （A00B~A00F 寄存器位定义），本记录为纯硬件实测反推，未依赖 datasheet。
5. **固化到固件**（当前 adb 替换为临时验证，重启还原）：
   ```bash
   source quectel_build/compile/build.sh
   buildconfig QSM565DWF QSM565DWFPARL1A01_BP01.001_Linux6.6.38_V01 <定制类型>
   bitbake yt6801 -c compile -f && bitbake quecpi-image && buildpackage
   ```
6. QCS6490 重启必须用 `adb shell reboot`（`adb reboot` 无效）。

---

## 9. 相关提交

```
db48675ed4 <master_r02><Feature><Igni>:YT6801网卡RJ45双色LED按速率动态指示
  (千兆黄灯常亮绿灯闪/百兆绿灯常亮黄灯闪)
```
