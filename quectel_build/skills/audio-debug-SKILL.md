# QCS6490 音频调试

## 音频调试（重要！）
dmesg 几乎没有音频日志，**必须用 journalctl** 查看 AGM/GSL 层完整调用链：

```bash
# 查看当前启动的 AGM/音频日志
journalctl -b --no-pager | grep -iE 'agm|audio|adsp|afe|gpr|glink|pcm|codec|i2s|mi2s'

# 查看 AGM 图形配置和启动日志（关键！）
journalctl -b --no-pager | grep -iE 'configure_i2s_ep|configure_codec_dma|graph_start|graph_prepare|sd_line|lpaif'

# 播放前后对比日志
dmesg -c > /dev/null
agmplay <文件> -D 100 -d 100 -i '<后端名>'
journalctl -b --no-pager | tail -30

# 检查声卡和 PCM 设备
cat /proc/asound/cards
cat /proc/asound/pcm

# 检查 AGM mixer controls
tinymix -D 100 controls | grep -iE 'MI2S|CODEC_DMA|DISPLAY_PORT|HDMI'

# 检查 ADSP 状态
cat /sys/class/remoteproc/remoteproc2/state
cat /sys/class/remoteproc/remoteproc2/name

# 检查 LPASS 时钟
cat /sys/kernel/debug/clk/clk_summary | grep -iE 'mi2s|lpaif|lpass_i2s'
```

## 音频播放命令（agmplay）
```bash
# Speaker 播放（必须先设置 tinymix，否则会失败！）
tinymix set "AUX_RDAC Switch" "1"
tinymix set "RX_MACRO RX0 MUX" "AIF1_PB"
tinymix set "RX INT2_1 MIX1 INP0" "RX0"
tinymix set "LO Switch" "1"
agmplay /tmp/test.wav -D 100 -d 100 -i "CODEC_DMA-LPAIF_RXTX-RX-0"

# HDMI 播放（不需要 tinymix）
agmplay /tmp/test.wav -D 100 -d 100 -i "MI2S-LPAIF-RX-PRIMARY"

# DP 播放
agmplay /tmp/test.wav -D 100 -d 100 -i "DISPLAY_PORT-RX"
```

## 测试前必须停掉 PipeWire
```bash
# 先停 GDM，否则 PipeWire 会被自动拉起
systemctl stop gdm 2>/dev/null
sleep 2
killall -9 pipewire pipewire-pulse wireplumber 2>/dev/null
sleep 2
# 确认已停止
ps -ef | grep -i pipe | grep -v grep
```

## AGM 常见错误码
| 错误码 | 含义 | 排查方向 |
|--------|------|----------|
| -131 | AR_ENOTREADY | ADSP 拒绝配置，检查 I2S 参数/sd_line_idx/ACDB 拓扑 |
| -22 | EINVAL | 参数无效，检查采样率/声道数/位宽/Channel Map mixer |
| -19 | ENODEV | 设备不存在，检查 PCM 设备号 |
| -5 | EIO | IO 错误，PCM 设备无法打开 |
| start error | pcm_start 失败 | 用 journalctl 查看 configure_i2s_ep/ configure_codec_dma 详细日志 |

## AGM 错误排查流程

1. **先用 journalctl 看完整错误链**：
   ```bash
   journalctl -b --no-pager | grep -iE 'agmplay|configure_|graph_|session_|error'
   ```

2. **常见错误链**：
   - `configure_i2s_ep` → `Graph cfg cmd failure` → error -131：ACDB I2S 端口配置与硬件不匹配
   - `configure_codec_dma_ep` → `Invalid mixer control` → error -22：Channel Map mixer 不存在
   - `pcm_plug_open: failed to open plugin` → error -5：PCM 设备打开失败

3. **需要向高通提 case 时**：
   - 抓 QXDM 日志：参考 `quectel_build/skills/qxdm-log-SKILL.md`
   - 提供 journalctl 错误链 + .qmdl 日志文件
