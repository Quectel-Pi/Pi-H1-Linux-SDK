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

## Speaker 无声：开机后第一次开流的竞态（已修复并实机验证）

**症状**：开机进桌面后 speaker 无声。`pw-play` 正常返回 0，音量/静音正常，
`wpctl status` 里 stream 也是 active；重启 pipewire 或切一次默认 sink 后恢复，
之后整个会话正常。

**判定特征**（root 看 pipewire 日志）：

```
COSI ar_osal_signal_timedwait: Failed to wait on signal, rc = 110
gsl  gsl_graph_open_sgids_and_connections: Graph open failed:21
pipewire graph_open: 749 exit, ret -110
pipewire pal_stream_start: 369: stream start failed. status -110
pipewire Reset path: speaker-vbat
```

`-110` 是 GSL 等 ADSP 上电超时；`-131` 是 I2S/ACDB 配置问题，两者别混。

**根因（在 pw-pal-plugin，不在内核/PAL）**：`pw_pal_stream_start()` 里
`pal_stream_start()` 失败时走 `cleanup:` 关闭 stream 就返回了；而它只由
`pw_pal_change_stream_state()` 在状态**跳变**到 `STREAMING` 时调用，
PipeWire 侧 stream 仍是 STREAMING，回调不再触发 → 插件永不重试。
结果是整个会话哑掉：PipeWire 照常丢 buffer，应用拿不到任何错误。

**修复**：`layers/meta-quectel/recipes-quectel/quectel/qcom-pw-pal-plugin/`
`0001-pw-pal-plugin-retry-stream-start-until-ADSP-ready.patch`
（已加进 `qcom-pw-pal-plugin_git.bbappend` 的 SRC_URI）

- 把 open+set_buffer+start 抽成 `pw_pal_stream_open_start()`；
- 失败时在 main loop 上挂 2s 定时器重试，最多 90 次（3 分钟，实测 ADSP 最慢 ~2.5 分钟才应答）；
- `PAUSED` / `ERROR` / 模块销毁时解除定时器并销毁 source（用 `pw_loop_add_timer` /
  `pw_loop_update_timer` / `pw_loop_destroy_source`），不引入新线程；
- 顺带修掉原函数里 `return rc;` 出现在 void 函数中的两处告警。

**验证（不需要设备、不需要 bitbake）**：

```bash
quectel_build/tools/check-pw-pal-patch.sh
# PASS  patch applies cleanly (git apply --check)
# PASS  patched source compiles (cross gcc -fsyntax-only, 5 warnings, 0 errors)
```

它用 SDK 里已构建好的 `sysroots-components` 交叉编译器和真实头文件做类型检查
（不是只做文本比对）。改动生效需重编该包：

```bash
source quectel_build/compile/build.sh
buildconfig QSM565DWF <版本号> LINUX STD      # 或 DEBIAN STD
bitbake qcom-pw-pal-plugin -c cleansstate && bitbake qcom-pw-pal-plugin
buildpackage
flash
```

**实机验证结果**（烧录后）：冷启动 3 次全部成功开流，其中 2 次日志为
`pal_stream_start failed, error -110` → `stream start failed (-110), retrying every 2000 ms`
→ `graph_open: 749 exit, ret -68` → `ret 0`；播放中途 `amixer` 读到
`LO Switch=on`、`RX_MACRO RX0 MUX=1`。热态连续 6 次播放零重试、零写入错误，
pipewire rss/fds 无漂移。

**已知边界**：重试依赖 stream 仍然活着。开机后第一声很短（1~2 秒的提示音）时，
应用已经退出、定时器随 `PAUSED` 解除，那第一声仍可能丢；之后的长流（音乐/视频/通话）
会自动在重试窗口内恢复。要连第一声也保证，只能在 ADSP 就绪前预热（试过，不可靠，
已放弃——预热的 agmplay 会与 PipeWire 的 PAL init 抢 ADSP，反而更容易触发 -110）。

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
