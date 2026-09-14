# QXDM 日志抓取 (smart_adb_qxdm_log)

## 工具位置

```
quectel_build/tools/smart_adb_qxdm_log/
├── defalutmask/default_logmask.cfg    ← 默认 mask
├── diag_logs/full-filter-audio.cfg    ← 音频专用 mask（推荐）
└── gnssmask/default_logmask.cfg       ← GNSS mask
```

## 快速使用

### 1. Push mask 文件到板子

```bash
# 音频问题用这个
adb push quectel_build/tools/smart_adb_qxdm_log/diag_logs/full-filter-audio.cfg /sdcard/diag_logs/Diag.cfg

# 通用问题用这个
adb push quectel_build/tools/smart_adb_qxdm_log/defalutmask/default_logmask.cfg /sdcard/diag_logs/Diag.cfg
```

### 2. 抓取日志

```bash
adb shell "
  # 停掉 GDM 避免 PipeWire 被拉起
  # （PipeWire 会随 GDM 自动重启，必须先停 GDM）
  systemctl stop gdm 2>/dev/null
  killall -9 pipewire pipewire-pulse wireplumber 2>/dev/null
  sleep 2

  # 清理旧日志和旧 session（重要！旧 session 会阻塞新 session）
  rm -rf /sdcard/diag_logs/20* /data/diag/*.qmdl 2>/dev/null
  pkill -9 diag_mdlog 2>/dev/null
  sleep 1

  # 启动 diag_mdlog（-f 后面必须跟文件路径）
  diag_mdlog -f /sdcard/diag_logs/Diag.cfg -o /data/diag/ -s 10 -e 2>&1 &
  DIAG_PID=\$!
  sleep 5

  echo '=== diag_mdlog started ==='

  # 在这里执行你要复现的操作，例如：
  agmplay /tmp/test.wav -D 100 -d 100 -i 'MI2S-LPAIF-RX-PRIMARY'

  sleep 3

  # 停止抓取
  kill \$DIAG_PID 2>/dev/null
  wait \$DIAG_PID 2>/dev/null

  echo '=== 日志文件 ==='
  ls -la /data/diag/*.qmdl
"

# 拉取到本地（统一存放到项目 log/ 目录）
mkdir -p log
adb pull /data/diag/diag_log_*.qmdl log/
```

### 3. Speaker 测试命令（需要先设置 tinymix）

```bash
adb shell "
  tinymix set 'AUX_RDAC Switch' '1'
  tinymix set 'RX_MACRO RX0 MUX' 'AIF1_PB'
  tinymix set 'RX INT2_1 MIX1 INP0' 'RX0'
  tinymix set 'LO Switch' '1'
  agmplay /tmp/test.wav -D 100 -d 100 -i 'CODEC_DMA-LPAIF_RXTX-RX-0'
"
```

## diag_mdlog 参数说明

| 参数 | 含义 |
|------|------|
| `-f <file>` | 指定 mask 配置文件（**必须带路径**） |
| `-o <dir>` | 输出目录（默认 `/data/diag/`） |
| `-s <sec>` | 循环日志间隔（秒） |
| `-e` | 启用 wake lock 防止休眠 |
| `-d` | 禁用控制台消息 |
| `-c` | 退出时清理 mask |
| `-a` | 禁用 HDLC 编码 |

## 板子上的前提条件

diag 走 QRTR 通道（不是老的 diagchar），需要确认：

```bash
# 检查 QRTR 驱动是否加载
lsmod | grep qrtr
# 应该看到：qrtr, qrtr_smd

# 检查 /dev/qrtr-ctl 是否存在
ls -la /dev/qrtr*
# 如果不存在，手动创建：
mknod /dev/qrtr-ctl c 102 0
```

## 不同场景的 mask 选择

| 场景 | mask 文件 | 说明 |
|------|-----------|------|
| 音频问题 | `full-filter-audio.cfg` | 包含 MSG_SSID_AUDIO, AFE, ADSP |
| 通用调试 | `default_logmask.cfg` | 默认 mask，消息较少 |
| GNSS 问题 | `gnssmask/default_logmask.cfg` | GNSS 专用 |

## 常见问题

### diag_mdlog 报 "No Session is active for the given mask"

上一次的 diag session 还没释放。先杀掉旧进程：
```bash
pkill -9 diag_mdlog
sleep 2
# 然后重新启动
```

### 日志文件很小（几百字节）

mask 文件没有正确加载。确保用 `-f` 且后面跟了文件路径：
```bash
# 正确 ✅
diag_mdlog -f /sdcard/diag_logs/Diag.cfg -o /data/diag/ -s 10

# 错误 ❌（-f 后面没跟文件名）
diag_mdlog -f -o /data/diag/ -s 10
```

### diag_mdlog 报 "could not open wakelock file"

非致命警告，可以忽略。只是 wake lock 功能不可用。

### 没有 /dev/qrtr-ctl

```bash
mknod /dev/qrtr-ctl c 102 0
```

## 日志文件说明

| 文件 | 说明 |
|------|------|
| `diag_log_*.qmdl` | 主日志文件，用 QXDM 打开 |
| `diag_qsr4_guid_list.xml` | QSR4 压缩日志的 GUID 映射 |

## 日志存放约定

所有调试日志统一存放到 **SDK 根目录下的 `log/` 目录**（即 `.hermes.md` 所在目录下的 `log/`）：
```
<SDK根目录>/log/
├── diag_log_*.qmdl        ← QXDM 日志
├── hdmi_error_log.txt     ← HDMI 错误链
├── dmesg_audio.txt        ← 内核日志
└── ...
```

抓完日志后执行：
```bash
# SDK根目录=$(git rev-parse --show-toplevel)
mkdir -p $(git rev-parse --show-toplevel)/log
adb pull /data/diag/diag_log_*.qmdl $(git rev-parse --show-toplevel)/log/
```

## 提供给高通的材料

1. `.qmdl` 日志文件
2. 复现步骤（agmplay 命令 + 错误输出）
3. `journalctl -b | grep -iE 'agm|afe|graph|configure_i2s'` 输出
4. `dmesg | grep -iE 'lpass|mi2s|afe'` 输出
5. 板子型号、内核版本、ACDB 文件
