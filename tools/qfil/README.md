# QFIL 烧录后端 (Windows)

本目录是 **Windows 下烧录固件** 所用的 QFIL 后端，来自 Qualcomm QPST 安装包
(`C:\Program Files (x86)\Qualcomm\QPST\bin`)。

| 文件 | 作用 |
|------|------|
| `QSaharaServer.exe` | Sahara 协议：把 firehose 程序 (`prog_firehose_Qcm6490_ddr.elf`) 推给处于 EDL(9008) 的设备 |
| `fh_loader.exe` | Firehose 协议：按 `rawprogram*.xml` / `patch*.xml` 分区表把镜像写入 UFS/eMMC |
| `reset.xml` | 烧录完成后复位目标的指令（见下） |

> 后端两个可执行文件为静态链接，导入表只有 `KERNEL32.dll`，
> **无任何额外 DLL 依赖**，可脱离 QPST 独立运行，合计约 740 KB。

## 用法

Windows 上直接运行批处理（或从 Linux/WSL 的 `scripts/flash.sh` 自动转交）：

```bat
scripts\flash.bat            :: 默认 UFS, 烧 build\result\ 下 SDK 自己编译的固件
scripts\flash.bat emmc
```

`scripts/flash.sh` 会检测平台：Linux/WSL 走 `tools/qdl`；Windows 走本目录后端
并经 `scripts/flash.bat` 调用；macOS 直接报错退出。

**固件目录**默认为 SDK 产物目录 `<SDK>\build\result`。
如需烧录外部固件包（例如厂商发布的测试固件），用环境变量覆盖：

```bat
set QPI_FW_DIR=D:\some\vendor\package
scripts\flash.bat
```

## 前置条件

1. 设备已进入 **EDL (9008)** 模式
   - 正常运行的系统：`adb shell reboot edl`（注意不是 `adb reboot edl`）
   - panic 状态：断电重新上电并按住 EDL 组合键
2. 已安装 Qualcomm USB 驱动（QPST 或 LIBUSB 驱动包）
3. 固件已打包完成：`build/result/` 下存在
   `prog_firehose_Qcm6490_ddr.elf`、`efi.bin`、`system.img`、`dtb.bin` 及 `partition_ufs/`

## 烧录后复位

厂商的 `rawprogram*.xml` / `patch*.xml` **不含 `<power>` 标签**，而 fh_loader 在
`--noprompt` 下**不会自动复位**。若不加处理，烧录完成后设备会**停留在 Firehose 模式**，
需手动断电重上电才能启动新固件。

`reset.xml` 就是为此提供的 —— 它被**追加到 `--sendxml` 列表的最后**：

```xml
<data>
<power DelayInSeconds="2" value="reset" />
</data>
```

fh_loader 的标签排序为 `<configure>,<erase>,others,<patch>,<power>`，
所以 `<power>` 一定在所有写入完成之后才执行。

用 `set QPI_NO_RESET=1` 可跳过（例如需要连续执行多个操作时）。

> Linux / WSL 侧无需此文件：`qdl` 默认就在烧录后复位，
> 其 `-R` / `--skip-reset` 才是跳过开关。

## WSL 下的烧录

**板子的 USB 接在 Windows 主机上，而 WSL2 默认不做 USB 直通**，因此 WSL 内的
`qdl` 看不到 9008 设备。`scripts/flash.sh` 会自动识别这种情况并转发到
Windows 侧的 `flash.bat`：

```
WSL 内:  ./scripts/flash.sh
         → 检测到 WSL2 环境
         → 转发到 Windows QFIL 后端
```

两种例外：

| 场景 | 行为 |
|------|------|
| 已在 WSL 内用 `usbipd-win` 直通 9008 | 直接用 `qdl`（脚本会检测到设备） |
| 设 `QPI_FLASH_LOCAL=1` | 强制用 `qdl` |

**固件无需预先复制。** SDK 位于 WSL ext4 时，Windows 侧经 `\\wsl.localhost\`
读取，实测 500 MB 镜像读取 290 MB/s，远高于 USB 烧录的约 41 MB/s，9P 不构成瓶颈。
SDK 位于 `/mnt/<盘符>` 时是普通 Windows 路径，更直接。

> 环境变量桥接：WSL 里 `export` 的变量不会自动进入 Windows 进程，
> 脚本会把 `QPI_FW_DIR` / `QPI_NO_RESET` 加入 `WSLENV` 后透传。

### 若要用 usbipd 直通（推荐给频繁烧录的场景）

在 Windows 侧（管理员 PowerShell）执行一次：

```powershell
usbipd list
usbipd bind --busid <设备ID>
```

之后每次设备进 9008 后：

```powershell
usbipd attach --wsl --busid <设备ID>
```

然后在 WSL 内 `./scripts/flash.sh` 会走 `qdl`。

## Windows COM 端口命名

**COM10 及以上必须写成 `\\.\COM10`**，裸 `COM10` 打不开端口：

```
QSaharaServer: port_connect:99 Failed to open com port handle
```

`scripts/flash.bat` 已统一转换。手工调用时请注意。

## 等效手工命令

```bat
QSaharaServer.exe -p \\.\COM10 -s 13:prog_firehose_Qcm6490_ddr.elf -v 1
fh_loader.exe --port=\\.\COM10 ^
              --sendxml=partition_ufs\rawprogram0.xml,partition_ufs\patch0.xml,...,tools\qfil\reset.xml ^
              --search_path=. --noprompt --memoryname=ufs --loglevel=1
```

## 实测记录

在 Quectel PI H1 (QCS6490) 实机上验证通过（厂商测试固件，UFS）：

```
[1/3] 检测到 Quectel QDLoader 9008 (COM10)
[2/3] Sahara: 推送 firehose 成功
[3/3] Firehose: 44 个条目写入 LUN 0-5
      {All Finished Successfully}
      Overall to target 332.328 seconds (40.80 MBps)

复位: Sending <power>
      TARGET SAID: 'INFO: bsp_target_reset() 1'
      → 9008 消失, ADB 与 RNDIS 网口枚举成功, 系统正常启动
```

未验证项：`emmc` 存储类型、`tools/qdl`（Linux/WSL 侧）路径尚未上板实测。

## 许可说明

`fh_loader.exe` / `QSaharaServer.exe` 为 Qualcomm 专有软件，版权归 Qualcomm
Technologies, Inc.。随 QPST 分发，仅限在授权设备上用于固件烧录。
