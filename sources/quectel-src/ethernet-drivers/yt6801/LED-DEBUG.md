# YT6801 RJ45 LED 调试记录（Quectel PI H1 / QCS6490）

> 板卡：Quectel PI H1 (QCS6490)
> 驱动：`sources/quectel-src/ethernet-drivers/yt6801`（裕太微 YT6801SH，v1.0.27）
> 原理图：`quectel_build/doc/Quectel-Pi-H1_SCH_V1.3_20251128.pdf` 第 12 页
> 初稿：2026-08-05 　重写：2026-10-12（板级逐位实测，推翻初稿的反推结论）

---

## 1. 问题现象

RJ45 双色灯行为与预期不符。**最终确认的预期**（用户口径）：

| 速率 | 绿灯 | 黄灯 |
|------|------|------|
| 1000M 已连接 | 闪烁 | 常亮 |
| 100M 已连接 | 常亮 | 闪烁 |

即两个速率下**角色互换**：千兆黄常亮+绿闪，百兆绿常亮+黄闪。

初始实测：**黄灯闪、绿灯不亮**（初稿记录为千兆下"全灭"）。

---

## 2. 硬件信息（原理图确认）

| 芯片引脚 | 信号名 | RJ45 灯 |
|---------|--------|---------|
| LED0 (PIN28) | RTL&YT_LED0 | **绿灯** |
| LED1 (PIN29) | RTL&YT_LED1 | 未接任何可见灯 |
| LED2 (PIN30) | RTL&YT_LED2 | **黄灯** |

灯阴极经电阻接地，芯片输出高电平点亮。板级实测 0x1800 等值可点亮，极性正常。

---

## 3. 根因分析

### 3.1 LED 配置机制

LED 行为由 **PHY 扩展寄存器**控制（MDIO 0x1E/0x1F 间接访问）：

| 寄存器 | 地址 | 说明 |
|-------|------|------|
| LED_CFG（公共） | 0xA00B | 公共配置（实测 0xE000） |
| LED0_CFG | 0xA00C | 绿灯配置 |
| LED1_CFG | 0xA00D | 未接 |
| LED2_CFG | 0xA00E | 黄灯配置 |
| LED_BLINK_CFG | 0xA00F | 闪烁配置（实测 0x0006） |

驱动启动从 **eFuse Byte0（EFUSE_LED_ADDR=0x00）** 读 5bit LED 方案索引：
`0x1F`→用 eFuse 存值；`0~4`→硬编码 SOLUTION0~4；**其他值→fallback SOLUTION0**。
对应 `fuxi-gmac-hw.c` → `fxgmac_release_phy()`（约 4415 行）。

### 3.2 板上实测

```
eFuse[0x00] = 0xaa  -> LED solution index = 0x0A
```

**0x0A 不在 0~4 也不等于 0x1F → 无效 → 落入 SOLUTION0**（LED0=0x2600 / LED2=0x00），
即初始的"黄灯闪、绿灯不亮 / 全灭"。

### 3.3 寄存器位定义（★ 修正初稿）

初稿把 `0x1800/0x2600` 当作"千兆/非千兆匹配编码"，是**错误反推**：本板实测
`0x2600` 在任何速率下都不亮。真实编码与 **mainline `drivers/net/phy/motorcomm.c`
的 YT8521/YT8531 逐位定义一致**（同门 PHY，同地址 0xA00C/0xA00D/0xA00E）：

```c
/* drivers/net/phy/motorcomm.c */
#define YT8521_LED_FDX_ON_EN     BIT(12)
#define YT8521_LED_HDX_ON_EN     BIT(11)
#define YT8521_LED_TXACT_BLK_EN  BIT(10)
#define YT8521_LED_RXACT_BLK_EN  BIT(9)
#define YT8521_LED_1000_ON_EN    BIT(6)
#define YT8521_LED_100_ON_EN     BIT(5)
#define YT8521_LED_10_ON_EN      BIT(4)
```

**关键结论 1：速率由 PHY 硬件按位门控**（bit4/5/6 只在对应速率下有效）。

**关键结论 2：活动位（bit9/10）单独写不闪，必须搭配一个当前速率成立的链路位。**
实测：100M 下 `0x600`（纯活动）不亮；加上 bit5 成 `0x670` 才闪。千兆下
`0x600` 不亮，加 bit6 成 `0x640` 才闪。即链路灯位是"底座"。

### 3.4 板级逐位实测（本次验证）

经 `/tmp/ytled`（see §7）逐位写入 `0xA00C` / `0xA00E`，肉眼观察：

链路 100M 时：

| 写入值 | 位 | 观察结果 | 结论 |
|--------|-----|---------|------|
| 0x00 | — | 灭 | 基线 |
| 0x20 | BIT5 | 常亮 | 100M 链路位 ✅ |
| 0x40 | BIT6 | 灭 | 1000M 链路位（100M 下门控关）✅ |
| 0x70 | BIT4\|5\|6 | 常亮 | 10/100/1000 链路位并集 ✅ |
| 0x1800 | BIT11\|12 | 常亮 | HDX/FDX 链路位 ✅ |
| 0x600 | BIT9\|10 | **不亮** | 纯活动位无链路底座，不工作 |
| 0x670 | BIT4\|5\|6\|9\|10 | 空闲常亮、有流量闪 | 绿灯(百兆)终值 ✅ |
| 0x2600 | — | 灭 | 初稿反推值，**废弃** |

链路 1000M 时：

| 写入值 | 位 | 观察结果 | 结论 |
|--------|-----|---------|------|
| 0x40 | BIT6 | 常亮 | 1000M 链路位 ✅ |
| 0x600 | BIT9\|10 | **不亮** | 同上：缺链路底座 |
| 0x640 | BIT6\|9\|10 | 空闲常亮、有流量闪 | 绿灯(千兆)终值 ✅ |

100M 下 `0x40` 灭、`0x20` 亮 → 证明 bit 是**速率门控**的，与 mainline 定义吻合。

---

## 4. 代码修改

### 4.1 修复无效 eFuse fallback（`fuxi-gmac-hw.c` ~4445 行）

eFuse 方案值无效（>4 且 !=0x1F）时直接写本板值，不再走 SOLUTION0。
此处尚无链路速率，先写 10/100M 默认（link up 后由 4.2 按速率修正）：

```c
/* 位定义与取值见 FXGMAC_LED_* 上方注释. 此处尚无链路速率,
 * 先写 10/100M 默认 (绿灯常亮/黄灯闪), link up 后再按速率修正. */
fxgmac_write_ephy_reg(pdata, REG_MII_EXT_ADDR, REG_MII_EXT_COMMON_LED0_CFG);
fxgmac_write_ephy_reg(pdata, REG_MII_EXT_DATA, FXGMAC_LED_GREEN_10_100);
fxgmac_write_ephy_reg(pdata, REG_MII_EXT_ADDR, REG_MII_EXT_COMMON_LED1_CFG);
fxgmac_write_ephy_reg(pdata, REG_MII_EXT_DATA, 0x00);
fxgmac_write_ephy_reg(pdata, REG_MII_EXT_ADDR, REG_MII_EXT_COMMON_LED2_CFG);
fxgmac_write_ephy_reg(pdata, REG_MII_EXT_DATA, FXGMAC_LED_YELLOW_10_100);
goto led_done;
```

> 有效方案值（0~4 / 0x1F）仍走原逻辑，不影响其他板型。

### 4.2 按速率配置函数（`fuxi-gmac-hw.c` ~4667 行）

期望行为在两个速率下**角色互换**（谁常亮、谁闪），所以按 speed 分支：

```c
#define FXGMAC_LED_GREEN_10_100   0x70  /* 10/100M 绿灯: 链路常亮 (不含活动位) */
#define FXGMAC_LED_YELLOW_10_100  0x670 /* 10/100M 黄灯: 100M 链路 + 收发活动闪 */
#define FXGMAC_LED_YELLOW_1000    0x40  /* 1000M 黄灯: 仅千兆链路常亮 */
#define FXGMAC_LED_GREEN_1000     0x640 /* 1000M 绿灯: 千兆链路 + 收发活动闪 */

static void fxgmac_config_led_under_speed(struct fxgmac_pdata *pdata, int speed)
{
    u16 green, yellow;

    if (speed >= SPEED_1000M) {
        green  = FXGMAC_LED_GREEN_1000;   /* 0x640 */
        yellow = FXGMAC_LED_YELLOW_1000;  /* 0x40  */
    } else {
        green  = FXGMAC_LED_GREEN_10_100;   /* 0x70  */
        yellow = FXGMAC_LED_YELLOW_10_100;  /* 0x670 */
    }
    /* ... 依次写 LED0/LED1/LED2 ... */
}
```

hw_ops 注册见 `fuxi-gmac-hw.c` ~6282 行（`led_under_speed`），link up 调用见
`fuxi-gmac-net.c` ~378 行。

---

## 5. 最终行为

| 协商速率 | 绿灯 LED0 | 黄灯 LED2 | 现象 |
|---------|-----------|-----------|------|
| 1000M | `0x640` | `0x40`  | **黄灯常亮 + 绿灯闪** |
| 100M / 10M | `0x70` | `0x670` | **绿灯常亮 + 黄灯闪** |
| 断开 | — | — | 两灯灭（链路灯位被 PHY 门控关掉） |

绿灯空闲时是常亮（链路底座），有流量才闪；闪烁频率由 `0xA00F`(LED_BLINK_CFG, 实测 0x0006) 决定。

---

## 6. 编译与验证

### 6.1 交叉编译 .ko（无需全量烧录）

```bash
ROOT=/home/igni/Downloads/debian/1.7-QuecPi-QCLinux-BL01
W=$ROOT/build-qcom-wayland/tmp-glibc/work/qcm6490_idp-qcom-linux
SC=$ROOT/build-qcom-wayland/tmp-glibc/sysroots-components/x86_64
KDIR=$W/linux-qcom-custom/6.6/build
SRC=$ROOT/sources/quectel-src/ethernet-drivers/yt6801

# 交叉工具链: gcc 与 binutils 分处两个 sysroot-components, 且 gcc 会 exec 裸 `as`
# （落到 host /usr/bin/as → "as: unrecognized option '-EL'"）。
# 用 COMPILER_PATH 强制 gcc 在交叉 binutils 目录里找 as/ld/objcopy...
ASBIN=/home/igni/.hermes/cache/scratch/asbin   # 里面放 as/ld/ar/nm/objcopy... 的软链
mkdir -p $ASBIN
for t in as ld ar nm objcopy objdump strip ranlib; do
  ln -sf $SC/binutils-cross-aarch64/usr/bin/aarch64-qcom-linux/aarch64-qcom-linux-$t $ASBIN/$t
done
export PATH=$SC/binutils-cross-aarch64/usr/bin/aarch64-qcom-linux:$SC/gcc-cross-aarch64/usr/bin/aarch64-qcom-linux:$PATH
export COMPILER_PATH=$ASBIN

BUILD=/home/igni/.hermes/cache/scratch/yt6801-build
rm -rf $BUILD && mkdir -p $BUILD && cp $SRC/*.c $SRC/*.h $SRC/Makefile $BUILD/
make -C $BUILD ARCH=arm64 CROSS_COMPILE=aarch64-qcom-linux- KDIR=$KDIR modules
# 产物: $BUILD/yt6801.ko  (vermagic 6.6.116-qli-1.7-ver.1.1)
```

### 6.2 adb 替换 + 重启

```bash
A=./quectel_build/tools/adb
M=/lib/modules/6.6.116-qli-1.7-ver.1.1/updates/yt6801.ko
$A push yt6801.ko /tmp/yt6801.ko
$A shell "cp -f $M /tmp/yt6801.ko.prev && cp -f /tmp/yt6801.ko $M && sync"
$A shell "reboot"          # ⚠ 必须 `adb shell reboot`, `adb reboot` 在本板无效
```

### 6.3 验证结果（本次实测）

```
$ adb shell dmesg | grep 'fxgmac led cfg'
fxgmac led cfg (speed 1000): led0=0x0640 led2=0x0040     # 千兆链路
fxgmac led cfg (speed 100):  led0=0x0070 led2=0x0670     # 百兆链路

$ adb shell ethtool eth0 | grep -E 'Speed|Link'
Speed: 1000Mb/s   Duplex: Full   Link detected: yes
# 30s 内 carrier_changes 19 -> 19 (稳定, 无 flap)

肉眼 (1000M): 黄灯常亮 + 绿灯闪            ✅ 符合预期
肉眼 (100M):  绿灯常亮 + 黄灯闪            ✅ 符合预期
# 闪 = 收发活动; 空口时对应灯常亮. 用 ping -f 打流量即可复现.
```

> 交叉编译脚本见 `log/yt6801-led/build-ko.sh`（一键出 .ko）。

---

## 7. 运行时调试工具（ioctl）

`log/yt6801-led/ytled.c`：通过驱动 debugfs 调试口 ioctl 读写 PHY 扩展寄存器，
免编译改灯（源码与说明见 `log/yt6801-led/README.md`）。

```bash
adb push log/yt6801-led/ytled.c /tmp/ && adb shell "cd /tmp && gcc -O1 -o ytled ytled.c"
/tmp/ytled read a00c / write a00c 670 / led 0 670
```

原理：打开 `/sys/kernel/debug/fuxi_0/yt6801/netdev_ops`，ioctl
`FXGMAC_SET_PHY_REG(0x10018)` / `FXGMAC_GET_PHY_REG(0x10017)`，扩展寄存器
先写 `0x1E`(EXT_ADDR) 再读写 `0x1F`(EXT_DATA)。

---

## 8. 遗留事项 / 注意

1. **千兆/百兆均已实测**：换用稳定千兆网线后协商到 `1000Mb/s` 并保持稳定
   （30s 内 `carrier_changes` 无变化），"黄灯常亮 + 绿灯闪" 与 "绿灯常亮 + 黄灯闪"
   两个场景都肉眼确认。
   > 首次换线前遇到过旧网线引发的链路 flap（约 10s 一次 up/down，`carrier_changes=17`），
   > 属线材/对端问题，与 LED 及本驱动改动无关。
2. **0x2600 已废弃**：初稿（commit `db48675ed4`）用 0x1800/0x2600 按速率分支的思路
   是本板反推误值，本次以 mainline 位定义 + 逐位实测取代。
3. **datasheet 不可得**：YT6801 datasheet 需向裕太微索取（官网下载中心需登录），
   1.0.27~1.0.34 驱动包均不含 A00B~A00F 位定义，只有不透明 SOLUTION0~4 常量。
   本次改用公开的 mainline `motorcomm.c`（同门 PHY 同地址逐位定义）作依据。
4. **固件默认是别的灯效？**：本改动同时兜住了 eFuse 无效值（0x0A）的情况；若日后
   eFuse 烧成有效方案值（0~4/0x1F），会回到厂商 SOLUTION 表（可能重新不一致）。
5. **固化到固件**：当前为 adb 临时替换，重启即还原（但 `updates/yt6801.ko` 已替换，
   重启不还原——注意：adb push 进 `/lib/modules/.../updates/` 是持久化的）。
   正式出固件：
   ```bash
   source quectel_build/compile/build.sh
   buildconfig QSM565DWF QSM565DWFPARL1A01_BP01.001_Linux6.6.38_V01 <系统> <版本>
   bitbake quecpi-image && buildpackage
   ```
