# VPU 视频解码调试 (Venus/iris_vpu)

> 会话交接文档：`log/VPU-交接-20260923.md`（含三仓状态、待办、复现命令、血泪教训）。
> 硬解修复补丁 v2 + 上板验证结论都在那里，接手前先读。

## VPU 硬件信息

- 解码器: `/dev/video32` (msm_vidc_decoder)
- 编码器: `/dev/video33` (msm_vidc_encoder)
- 媒体控制器: `/dev/media0` (msm_vidc_media)
- 驱动: `msm_vidc_v4l2` (iris_vpu)
- 固件: `/usr/lib/firmware/qcom/vpu-2.0/venus.mbn`
- 平台: `aa00000.video-codec`

## 支持的编解码格式

| 方向 | 格式 | V4L2 FourCC |
|------|------|-------------|
| 解码输入 | H.264 | H264 |
| 解码输入 | HEVC | HEVC |
| 解码输入 | VP9 | VP90 |
| 解码输入 | HEIC | HEIC |
| 解码输出 | NV12 | NV12 |
| 解码输出 | NV21 | NV21 |
| 解码输出 | Q08C (高通压缩) | Q08C |
| 编码输出 | H.264 | H264 |
| 编码输出 | HEVC | HEVC |

## VPU 状态检查命令

```bash
# 检查 VPU 设备
v4l2-ctl -d /dev/video32 --all
v4l2-ctl -d /dev/video32 --list-formats-out    # 解码输入格式
v4l2-ctl -d /dev/video32 --list-formats         # 解码输出格式
v4l2-ctl --list-devices                         # 所有 V4L2 设备

# 检查 VPU 驱动状态
dmesg | grep -iE 'vidc|venus|vpu|iris' | tail -20

# 检查 VPU 固件
ls -la /usr/lib/firmware/qcom/vpu-*/

# 检查 VPU 会话数（内核日志）
dmesg | grep 'session' | tail -10
```

## VPU 会话泄漏修复（重要！）

### 泄漏怎么来的（实测归因，2026-09）

**触发条件 = 客户端在流还在跑的时候退出。** 不等于"只有被强杀才会"：

| 播放器怎么退的 | 结果（4K，hwdec=vaapi） |
|---------------|----------------------|
| 自然播完（EOS 自己退） | 实例 0 — **不漏** |
| **点窗口关闭按钮（SIGTERM）** | 实例 1，`state=4`(CLOSE) — **漏** |
| `kill -9`（SIGKILL）/ 崩溃 / OOM | 实例 1，`state=4`(CLOSE) — **漏** |

**"关窗口"这个最日常的动作本身就在漏** —— 播放器收到 `SIGTERM` 就退，不做流级
收尾，会话留在 CLOSE(4)。所以用户"手动播了几次就废了"，不是他操作有误。
播放中关窗口与 `kill -9` 后果完全相同，别以为前者安全。

固件层内存泄漏，驱动只是报信者：

```
msm_vidc: fw: <VFW_E:HostDr:264d:76652fc0:00> VenusHostDriver_ParseC2Command(1229):
  Memory leak found: current heap status: 1292e, expected heap status : 0
```

所以**停播放器做测试时，`pkill -x mpv`（TERM）与 `pkill -9 mpv` 后果一样**，都漏。
要么让片子自然播完（EOS），要么每轮测量前先按下面「修复」重置 VPU —— 否则你会把
上一轮的污染当成"本轮新故障"。

### 死会话是什么

被 kill 的实例会留在 `/sys/kernel/debug/msm_vidc/core/inst_*`，`state` 取值
（`vidc/inc/msm_vidc_state.h` 的 `FOREACH_STATE`）：

```
OPEN=0  INPUT_STREAMING=1  OUTPUT_STREAMING=2  STREAMING=3  CLOSE=4  ERROR=5
```

僵尸会话 = **4 (CLOSE)**，失败探测的残留 = **5 (ERROR)**。两者都是终态
（ERROR 的全部迁移是 `MSM_VIDC_IGNORE`，CLOSE → OPEN/STREAMING 是
`MSM_VIDC_DISALLOW`），固件永远不会回收它们，**但它们照样被算进负载**。

### 真正的闸门是 MBPF（每帧宏块数），不是会话条数

`msm_vidc_check_session_supported()` 里按顺序过 5 道闸，真正拦住 4K 的是
**第 2 道 `msm_vidc_check_core_mbpf()`**，不是会话计数那道：

```
[  60.890933] msm_vidc: err : 7461afc0: avcD_0: msm_vidc_check_core_mbpf: video overloaded. needed 97200, max 77522
[  60.890946] msm_vidc: err : 7461afc0: avcD_0: msm_vidc_check_session_supported: current session not supported
[  60.890982] msm_vidc: err : 7461afc0: avcD_0: msm_vidc_streamon: vb2_streamon(10) failed, -12
```

算式：4K 一帧 = 240 x 135 = **32400** 宏块；`MAX_MBPF = 77522`，只够 **2 个**
4K 负载（3 x 32400 = 97200 就爆）。所以额度是"2 个 4K 负载"，死会话本来在
跑 4K，就还占着这 2 个名额之一。mpv 这时静默回落软解（日志里
`Using hardware decoding (vaapi)` 消失），**而且这次失败又留下一个新的 ERROR
会话**，越试越糟（棘轮）。

实测（4K H.264，播放中查实例数 + 看 mpv 自报解码方式）：

| 死会话 | 播放中实例 | mpv 自报 |
|--------|-----------|---------|
| 0 | 1 | `Using hardware decoding (vaapi)` |
| 1 | 2 | `Using hardware decoding (vaapi)` |
| 2 | 3 | 无 → 回落软解，dmesg `streamon失败=2` |

> **别只看 `msm_vidc_check_max_sessions`。** `MAX_NUM_4K_SESSIONS = 2` 那道闸
> 确实也在数死会话，但把死会话从它里面剔掉**并不能**修好问题 —— mbpf 那道闸
> 先拦（实测：只改 `check_max_sessions` 的 .ko 上板行为与出厂版一字不差）。
>
> ⚠️ **也别去改 mbpf/负载统计去"跳过死会话"** —— 见下面「治本」一节：死会话
> 确实占着固件堆，跳过它 = 放开超配，实测死会话到 5 个时播放中把板子硬复位。
> 这个闸门本身是保护，要修的是**泄漏本身**。

### 修复：不需要重启设备

> ⚠️ **前提：先确认没有任何客户端持有 VPU。** 播放中做 unbind 会**当场把板子打挂**
> （硬复位，连 dmesg 都来不及落盘）。实测 `sync` 过的日志断在 unbind 那一行：
>
> ```
> STEP1 播放中 实例=1 states=[3] mpv=1     ← state 3 = STREAMING
> STEP2 即将 unbind（客户端仍持有 VPU）
> （STEP3 从未写出，板子当场复位）
> ```
>
> 顺序错了就是这个后果。**必须先停播放器、确认 `pgrep -x mpv` 为空，再 unbind。**

```bash
# 1. 先停播放器，并确认真的没有了（漏 export 会导致 mpv 秒退，别误判）
pkill -x mpv; sleep 3; pgrep -x mpv | wc -l     # 必须是 0

# 2. 再重置 VPU
echo aa00000.video-codec > /sys/bus/platform/drivers/msm_vidc_v4l2/unbind
sleep 2
echo aa00000.video-codec > /sys/bus/platform/drivers/msm_vidc_v4l2/bind
sleep 3

# 3. 验证：实例数应回到 0
ls -d /sys/kernel/debug/msm_vidc/core/inst_* 2>/dev/null | wc -l
```

**unbind/rebind 是真还额度，不是只清 debugfs 目录** —— 用 mpv 判据实测：
2 个死会话时播放中实例=3、mpv `Using hardware decoding` 消失、dmesg 2 次
`streamon failed`；unbind/rebind 后实例回到 0，再播 4K 是实例=1 +
`Using hardware decoding (vaapi)`、失败行 0、dmesg 0 次 streamon 失败。
（这一组是在**无客户端持有 VPU** 时做的，所以安全；有客户端时见上面的 ⚠️。）

**别指望 `trigger_ssr`** —— 实测用它是 3 个死会话进、3 个出，无效。

只有 unbind/rebind 也拉不回来（`Power on failed`）才需要重启设备：
```bash
adb shell reboot   # 注意：用 adb shell reboot，不要用 adb reboot
```

### 治本：关闭路径上的两个缺陷（补丁已上板验证）

配方侧的补丁（`layers/meta-quectel/recipes-multimedia/video/`）：

```
qcom-videodlkm_1.0.bbappend
qcom-videodlkm/0001-vidc-stop-streaming-and-release-the-queues-when-a-session-is-closed.patch
```

缺陷都在 `msm_vidc_close()`（`vidc/src/msm_vidc.c`）上，都在"客户端没做流级收尾"时
暴露，补丁各治一处：

**① 会话关闭时队列还在 streaming。** `msm_vidc_close()` 直接把
`HFI_CMD_SESSION_CLOSE` 发给固件，此时两个队列可能都还在 streaming；而停队列只发生在
`msm_vidc_close_helper()` 的队列释放里，那时固件会话已经没了 —— 太晚，固件只能报
`Memory leak found`。补丁在 `msm_vidc_session_close()` 之前新增
`msm_vidc_stop_streaming_before_close()`，先停两个队列（先输出/帧、后输入/码流，与队列
释放同序）。停流内部是 `inst_unlock` + 超时等待，所以在关闭路径里调用**不会死锁**。

**② 引用环：队列释放只发生在引用计数归零之后。** `msm_vidc_vb2_queue_deinit()` 只在
`msm_vidc_close_helper()` 里被调用，而 `close_helper` 只在 kref 归零时才跑；可是
buffer 自己持有 inst 的引用（`msm_vb2_alloc()` 拿、`msm_vb2_put()` 放，后者在队列释放
时才跑）。客户端没做 `VIDIOC_REQBUFS(0)` 就退出 → 环闭死 → 实例永远留在
`core->instances` 的 CLOSE 态、继续吃额度（**这是额度真正被吃掉的原因**）。补丁在最后
`put_inst()` 之前先调 `msm_vidc_vb2_queue_deinit()`（幂等：`m2m_dev` 为 NULL 直接返回，
所以 `close_helper` 里那次退化成空操作）。

**两条都要打**：只打①，固件侧干净了（`Memory leak found=0`）但实例照样留、仍 1→2→3
累加、第 3 次 4K 就回落软解；只打②，固件堆照样漏。

上板验证（干净启动 → 装新 .ko → 4K H.264，每轮播放中 `pkill -9 -x mpv`）：

```
ko md5=cbc8d645f4385cc22c1e53ffee20f13b
起始 实例=0
第1..4次 kill -9: 实例=0  Memory-leak-found=0  本次 Using hardware decoding (vaapi)
四次泄漏尝试之后 4K: 实例=1  Using hardware decoding (vaapi)  streamon失败=0
收尾 实例=0
```

三种 4K 编码同样通过（播放中实例=1、kill 后实例=0、泄漏报告 0、硬解标记 1）：
`h2644k20 / hevc4k20 / vp94k20`。并发 3 个 4K（第 3 个被 mbpf 正常拒绝、回落软解）
全部 kill 后实例也回到 0 —— 以前"每次被拒再永久留一个 ERROR 会话"的棘轮也没了。

**别改成"统计时跳过死会话"（这个方向已被实测推翻）**：死会话确实占着固件堆，跳过
它等于让下一个会话在真正 OOM 的固件上启起来 —— 实测死会话 0..4 时 4K 硬解正常、
**第 5 个的时候播放中板子硬复位**（主机侧抓 `/dev/kmsg` 无 oops/panic ⇒ 固件侧超配，
不是内核崩），且每次被拒又多留一个 ERROR 会话。

## VA-API 驱动 (msm_drv_video.so)

把 VA-API 解码请求转成 msm_vidc 的 V4L2 M2M 调用，让 ffmpeg/mpv 等应用直接用硬解。

- 源码与构建: `layers/meta-quectel/recipes-multimedia/msm-va-driver/`（`files/` 是源码，
  `build.sh` 在板子上编译安装，`verify.sh` 是配套验证脚本，README 有完整状态）
- 上游: https://github.com/snowf14k3/venus-vaapi-driver (MIT)，本项目的适配全部在
  fork `git@github.com:super617/venus-vaapi-driver.git` 的分支 `qcm6490-msm-vidc-adapt`
  上；`qcm6490-msm_vidc-adapt.patch` 是同一份改动对上游 `47744788` 的 patch 视图（给人 review
  用，recipe 不 apply 它）
- 安装位置: `/usr/lib/aarch64-linux-gnu/dri/msm_drv_video.so`
- 进镜像方式: recipe 从 GitHub fork 编译（`msm-va-driver_1.0.bb`，`SRCREV` 钉死），
  `quecpi-image.bb` 的 `IMAGE_INSTALL` 里加了 `msm-va-driver`，同时装 `${libdir}/dri`
  和 Debian multiarch 两个路径。**仓库里不再放预编译 .so**（`prebuild/bsp-fix/` 下已无该文件）

libva 会按 DRM 驱动名 `msm` 自动找到这个文件，**不需要**设 `LIBVA_DRIVER_NAME`。

### 安装与验证

```bash
apt-get install -y libva-dev          # 板子上需要 va.h 和 libva.so
cd layers/meta-quectel/recipes-multimedia/msm-va-driver
./build.sh --install                  # 或 make 交叉编译后拷贝 .so
vainfo                                # 应显示 "Qualcomm Venus stateful V4L2 VA-API backend"
./verify.sh                           # 从仓库源码重建 + 硬解/软解逐字节比对
```

**进镜像**：driver 由 recipe 从 GitHub fork 编译进固件（见上），烧录后 `dri/` 下就有
`msm_drv_video.so`，不需要在板子上编译。改了 `files/` 后要先推 fork 并更新 bb 的 `SRCREV`，
固件才会带上新版本；板子上临时验证用 `./build.sh --install`。

### 使用方法

```bash
# ffmpeg 解码（必须带 -hwaccel_output_format vaapi，且 stdin 重定向）
ffmpeg -hwaccel vaapi -hwaccel_device /dev/dri/renderD128 \
  -hwaccel_output_format vaapi -f h264 -i input.h264 -f null - < /dev/null

# 导出 NV12 比对（hwdownload 是必需的下载步骤）
ffmpeg -hwaccel vaapi -hwaccel_device /dev/dri/renderD128 \
  -hwaccel_output_format vaapi -f h264 -i input.h264 \
  -vf hwdownload,format=nv12 -pix_fmt nv12 -f rawvideo out.nv12 < /dev/null

# mpv 零拷贝硬解（surface 以 DMA-BUF 导出，GPU 直接当 EGLImage 采样）
mpv --hwdec=vaapi video.mp4
# 遇到含 B 帧的流回落到软解时改用 copy 模式
mpv --hwdec=vaapi-copy video.mp4
```

### 实测状态（QCS6490, 6.6.116-qli-1.7-ver.1.1）

可用：

- `vainfo` 报告 H.264 Baseline/Main/High 的 VLD 与 EncSlice，以及 HEVC Main 的 VLD 与
  EncSlice、VP9Profile0 的 VLD（VP9 编码内核不支持，未列出）
- **HEVC / VP9 硬解与软解逐字节一致**（HEVC 1280x720 58 帧、1920x1080 60 帧；VP9
  1280x720 300 帧；`cmp` 全等），H.264 硬解回归同样一致
- **HEVC 硬编可用**：`ffmpeg -vaapi_device /dev/dri/renderD128 -vf format=nv12,hwupload
  -c:v hevc_vaapi -rc_mode CBR -b:v 4M`（CBR 是内核编码器唯一支持的模式；
  `-vf format=nv12,hwupload` 必须显式给，自动插入的 `scale_vaapi` 需要 VPP，本驱动没有）
- **无 B 帧的 H.264 解码与软解逐字节一致**（640x480 baseline、1280x720 High 均验证过；
  baseline 720p 98MB NV12 输出 `cmp` 全等）
- 100Mbps 4K H.264（3840x2160，无 B 帧）整段能播（`mpv --hwdec=vaapi-copy` 到 EOS），
  ffmpeg 走 VPU 解码 41fps(1.37x 实时)，CPU 时间只有软解的 1/9
- ffmpeg 命令行能解到流尾并 `hwdownload` 出 NV12
- **`vaExportSurfaceHandle` 可用**：surface 从 `/dev/dma_heap/`（`qcom,system`，
  退到 `system`）分配，以线性 NV12 的 DMA-BUF 导出，应用可 mmap 或作为 EGLImage 导入
- **mpv 零拷贝硬解上屏可用**：`Using hardware decoding (vaapi)` +
  `[vo/gpu/vaapi] Using EGL dmabuf interop via GL_EXT_EGL_image_storage`。
  上屏画面用截图逐像素比对过：四象限颜色/几何正确，与软解渲染差异 ≤1 LSB（仅舍入）

不可靠 / 不支持：

- **含 B 帧的重排序流**：`./verify.sh` 每次跑 10 轮，同一段 150 帧 720p `-bf 3` 的
  逐字节通过率在 3/10 ~ 9/10 之间抖（写这套东西期间累计 52/80 ≈ 2/3，从外部无法预知
  这一轮是哪边）。失败轮次就是下面的「客户端 surface 池 ↔ 解码器重排序持有」死锁：
  drain 能救回码流，但冲掉的参考帧会让该段画面异常或报 23 号错误回落软解
- **VLC 零拷贝硬解上屏可用**（2026-09-21 已修）：`vlc` 默认（不加 `--avcodec-hw`）就会
  `Using Qualcomm Venus stateful V4L2 VA-API backend ... for hardware decoding`，
  4K30 稳定 30fps、零丢帧。曾表现为「有窗口、进度条在走、画面整块纯绿」，原因见下面
  「7. VLC 绿屏 / 用不上硬解」
- 驱动仍把画面从 V4L2 buffer memcpy 进 VA surface（`files/src/decode.c` 的 memcpy），
  这段拷贝未消除；`vaExportSurfaceHandle` 消掉的是播放器再上传 GPU 那一份
- 编码只支持 H.264 / HEVC，而且只有 CBR：内核编码器不提供其它码率控制；VP9 编码内核不支持
- **SPS 里带短时/长时参考图像集（ST-RPS / LT-RPS）的 HEVC 流不能硬解**：VAAPI 客户端只
  传解析后的字段、不传 VPS/SPS/PPS，驱动只能从 `VAPictureParameterBufferHEVC` 重建参数集，
  而参考图像集的内容不在其中，这类流直接返回「不支持」而不是照错的参数集解。x265 等软编码
  输出可用，**板子自身硬编出来的 HEVC 属于被拒的那类**（`dmesg` 里能看到
  `H265_CONFIG_FLAG_MISSING` 是参数集完全没下发时的表现）

### 4K 实测（DJI_0010.MP4：3840x2160 H.264 High 100Mbps，617 帧，21 I + 596 P 无 B 帧）

整段 617 帧，每种配置各跑 3 次（稳定在 ±1fps）：

| 配置 | 4K 解码速率 | CPU 时间 |
|------|------------|---------|
| `ffmpeg -hwaccel vaapi ...`（默认：每核一个 hwaccel 线程） | 40-41fps | 12.9s |
| `ffmpeg -threads 1 -hwaccel vaapi ...` | 71fps | 6.4s |
| 1080p，默认 | 70fps | 8.5s |
| 1080p，`-threads 1` | 335fps | 2.2s |

**结论 1（免费收益）：`-threads 1` 在 4K 上 1.75x、1080p 上 4.8x。** 默认多线程时多个
VA context 同时下载解码帧，而这下载就是 CPU 读 V4L2 capture buffer —— 实测 1.32GB/s，
同样的拷贝在普通内存里 10.8GB/s（即 capture buffer 是非缓存映射），所以并发读互相
拖慢：驱动那份拷贝 `-threads 1` 时 9.4ms/帧，默认线程数时 20.9ms/帧。

**结论 2：`-threads 1` 下这份拷贝完全被 VPU 掩盖，4K 天花板就是 VPU。** 把拷贝整个
去掉（仪表化构建，`VENUS_COPY_MODE=3`）4K 仍是 71fps，1080p 只从 335 涨到 357fps。
71fps 已超过 QCS6490 的 4K60 规格 → 4K 没有更多 fps 可挖。去掉拷贝省的是 CPU（每 4K 帧
9.4ms、约 89%），不是 fps。

**结论 3：4K 掉帧是上屏（合成器）问题，不是解码。** 4K30 片源下 `mpv --hwdec=vaapi` 掉
247-258/617，`--hwdec=vaapi-copy` 只掉 100-103 —— 但这是把起播丢帧一起算的读数：稳定期
（见下节）4K30 两者都是 0 掉帧，4K60 时结论反转（直出 46fps vs copy 18.6fps）。所以
**4K 应优先 `--hwdec=vaapi`（直出）**，早先那句"优先 `--hwdec=vaapi-copy`"是拿 4K30 片源
测不出差别导致的误判。

### 上屏路径才是掉帧来源，已固化 mpv 配置

#### 测量方法（先排除起播丢帧，否则结论会反）

mpv 的 `frame-drop-count` 把起播阶段的丢帧也算进去。4K60 起播要 7-9s（解码器初始化 + VO
配置），这期间视频时钟照走，一次性计入几百帧假丢帧 —— 不排除就会得出"直出比 copy 更差"
这类反向结论（本 skill 早先那版 4K30 表格就是这么来的）。

正确做法：经 `--input-ipc-server` 轮询 `time-pos`，等它 > 6 秒（真正进入稳定播放）再统计
8-12s 窗口内的 `frame-drop-count` 增量。脚本在板上 `/var/tmp/b3.sh <片源> <标签>`：

```bash
DUR=8 EXTRA="--no-config --vo=dmabuf-wayland --hwdec=vaapi" /var/tmp/b3.sh /var/tmp/vatest/h2644k20.mp4 tag
#   tag | vo=dmabuf-wayland hwdec=vaapi | 窗口=8s 解码=60.1fps 显示=46.0fps 丢帧=113(14.1/s) | 会话=1
```

A/B 对比必须两边都带 `--no-config`：profile 在运行时应用，命令行给的 `--vo`/`--hwdec` 会被
profile 覆盖（实测 `--vo=gpu` 仍然报 `current-vo=dmabuf-wayland`）。

#### 稳定期实测（面板 1920x1080@60，片源 4K60 H.264 1200 帧）

| 内容 | `--hwdec=vaapi-copy`（原配置） | `--hwdec=vaapi`（直出） |
|------|------------------------------|-----------------------|
| 4K30 DJI 617 帧 | 30.0fps，丢 0 | 30.0fps，丢 0（CPU 只有一半） |
| 4K60 H.264 | **18.6fps**，丢 39.5/s | **46.0fps**，丢 14.1/s |
| 4K60 HEVC | **18.4fps**，丢 38.1/s | **45.2fps**，丢 15.2/s |
| 4K60 VP9 | 未测（早期含起播污染的读数 12.8fps） | 47.5fps |
| 1080p60（走 vo=gpu，未改） | 48.1fps | - |

4K60 场景直出是 copy 的 **2.5x**，4K30 两者都满帧 → `hwdec=vaapi` 在 dmabuf 路径下没有代价。
原因：`vo=dmabuf-wayland` 直接把解码器 buffer 交给合成器，`vaapi-copy` 却先把 4K NV12 下载到
系统内存（12.4MB/帧，60fps 就是 745MB/s 的 memcpy）再丢掉，纯浪费。

#### 剩下那 ~46-48fps 的天花板与像素无关

同一个 1080p60 源：全屏 46.1fps、640x360 小窗 46.4fps，丢帧数完全相同（各 111 帧/8s）；
4K60 是 46fps、1080p 是 48fps，也几乎一样。→ 瓶颈是每帧一笔固定开销（合成器提交/回调
节奏），不是填充率、不是 4K→1080p 缩放、不是解码。GPU 也没满（`simple_ondemand` 停在
315/550MHz）。要突破得动合成器（GNOME Shell 48.7 / mutter 16），不是 mpv 参数。

#### `--video-sync=display-vdrop` 是陷阱，不要全局开

| 内容 | 默认（display-resample） | `display-vdrop` |
|------|------------------------|-----------------|
| 4K60 HEVC | 45.2fps | 59.5fps |
| 4K30 DJI | 30.0fps，丢 0 | **25.1fps** |

vdrop 以显示时钟为唯一参考、迟到帧直接丢：60fps 内容收益明显，30fps 内容因为要和 60Hz 的
抖动对齐反而掉到 25fps。带音频的同一组读数一致（4K60 54.2fps、4K30 25.5fps）。

#### 固化位置

`prebuild/gnome/etc/mpv/mpv.conf`（`gnome` 在 `prebuild/sync-list` 里、排在 `bsp-fix` 之后，
所以它是 desktop 配置的最终话事层）；Ubuntu 侧 `prebuild/ubuntu26/etc/mpv/mpv.conf` 需同步，
两份文件除头部同步说明外内容一致。

```ini
hwdec=vaapi-copy

[large-frames]
profile-desc=DMABUF passthrough for >=1440p sources
profile-cond=width ~= nil and height ~= nil and (width >= 2560 or height >= 1440)
profile-restore=copy
hwdec=vaapi
vo=dmabuf-wayland,gpu
```

（代码块里不带注释：**mpv.conf 只认行首 `#`，行内 `#` 会被当成值的一部分**，实际文件里
这两行 `hwdec` 的解释写在文件头部的注释里。）

小分辨率保留 `vaapi-copy` 的原因：那条路径走 `vo=gpu`，Mesa 在这块板子上导入不了解码
dmabuf（`Failed to set BO metadata with DRM_MSM_GEM_INFO: -22`），反正都要下载，copy 只
多花 CPU。`large-frames` 里改 `vaapi` 是直出，4K60 从 18.6fps 提到 46fps。

实测（无任何命令行参数，只靠这个配置）：

| 内容 | 稳定期掉帧 | 走的 vo |
|------|-----------|--------|
| 4K30 DJI | 0 | dmabuf-wayland |
| 4K60 H.264 | 170 帧 / 12s（45.9fps） | dmabuf-wayland |
| 1080p60 | 145 帧 / 12s（48.1fps） | gpu（保留字幕/OSD） |

四个坑：
- **`profile-cond` 必须判 nil**。`width`/`height` 在流信息就绪前是 nil，直接比较会抛
  Lua 错误，mpv 报 `Errors when loading file` 直接中止播放（4K60 就这样挂过）。
- **mpv 默认 `hwdec=no`**，不写这个文件等于全程软解。
- **改完要重新打包镜像才固化**，改板上的 `/etc/mpv/mpv.conf` 只是临时验证（镜像里那份是
  从 `prebuild/` 铺进去的）。
- 按分辨率分流的原因：`dmabuf-wayland` 只渲染视频，不支持 OSD/字幕；所以只在
  大分辨率时切过去，小分辨率留在 `vo=gpu`。

### totem（GNOME 视频播放器）没有等价的配置项

totem 走 `playbin`，没有 `hwdec`/`vo` 这类开关，等价维度是"元素选择"，而元素是写死在
libtotem 里的。全部可改项只有三个：

| 旋钮 | 位置 | 作用 |
|------|------|------|
| `force-software-decoders` | gsettings `org.gnome.totem` | **只能强制软解**（反方向），4K60 实测掉到 0.62x 实时、CPU 417% |
| `TOTEM_USE_GST_GTKSINK` | 环境变量 | 强制走 `gtksink`（GdkTexture 上传），不走 `gtkglsink` |
| `GST_PLUGIN_FEATURE_RANK` | 环境变量 | GStreamer 通用：换解码器（默认已选到 `vah264dec` 硬解） |

硬件解码不用配也是开的：`playbin`→`decodebin3` 按 rank 自动选 `vah264dec`。sink 侧没有
任何选项——libtotem 里写死顺序 `gtkglsink` →（检测到软件 GL 光栅化器）→ `gtksink`，
没有 waylandsink/dmabuf 直通可选。`~/.config/totem/state.ini` 只记窗口大小，与播放无关。

测 totem 的帧率/丢帧：它没有 mpv 那样的计数器，也没有 IPC，用 MPRIS 取播放位置，比
"媒体时钟 vs 挂钟"（跟不上就会 <1.0x），同时用 QoS 丢帧（`gstvideodecoder` 的
`Dropping frame due to QoS` 是 WARN，不需要开 GST_DEBUG）。脚本 `/var/tmp/t3.sh <片源> <标签>`。
注意 totem 的 `Position` 返回 `(<int64 123>,)`——解析时先 `sed 's/int64//'`，否则
grep 数字会命中 "64"。另外 totem 常在启动窗口期停在 Paused，需要显式
`org.mpris.MediaPlayer2.Player.Play`。

4K60 H.264 实测（面板 1920x1080@60）：

| 配置 | 媒体时钟 | VPU 解码 | QoS 丢帧 | CPU |
|------|---------|---------|---------|-----|
| 默认（`gtkglsink`） | 1.00-1.03x | 62fps | 0 | 167% |
| `TOTEM_USE_GST_GTKSINK=1` | 0.97x | 58fps | 0 | **122%** |
| `force-software-decoders=true` | 0.62x | 0 | 0 | 417% |

`gtkglsink` 那条路会打 `Failed to set BO metadata with DRM_MSM_GEM_INFO: -22`——和 mpv
`vo=gpu` 撞的是同一个 Mesa 导入失败，所以它也是"下载再上传"，CPU 比 mpv 直出（76%）高一倍。
想省 CPU 就把 `TOTEM_USE_GST_GTKSINK=1` 写进会话环境（`/etc/environment` 或 prebuild
overlay），实测 4K60 照样 1x 实时且不报丢帧。

**但 totem 的"0 丢帧"不能直接和 mpv 的读数比**：GStreamer 的 sink 不等 presentation
反馈，帧画进 GL area 就算成功，合成器有多少帧没真正上屏它不知道；mpv 用显示时钟会把这类
迟到计成丢帧（4K60 直出 46fps / 丢 14.1/s）。所以上屏天花板到底在哪，两边都给不出
干净判词，要动的是合成器（见上节）。

### 怎么证明真走了 VPU（而不是偷偷软解）

解码跑起来时查这三个：

```bash
pid=$(pgrep -f ffmpeg | head -1)
ls -l /proc/$pid/fd | grep video        # -> /dev/video32（只有本驱动会开它）
cat /sys/kernel/debug/msm_vidc/core/inst_*/info
#   INSTANCE ... (Decoder)   width: 3840   height: 2176
#   ETB/EBD/FTB/FBD 计数在涨
fuser -v /dev/video32
```

实测这条 4K 流：6 秒窗口内 VPU 交付 242 帧（40.2fps），ffmpeg 报告 39.4fps —— 两者同步，
说明是应用在限速而不是 VPU（这几个计数器是累计值，采样两次相减，别直接把总数当速率）。
注意 debugfs 那几个文件要用 `cat`，`head` 会报 "cannot seek to relative offset"。

## V4L2 M2M 直接测试

不通过 VA-API，直接使用 V4L2 API 测试 VPU 解码:

```bash
# 编译
gcc -o v4l2_dec_test v4l2_dec_test.c

# 运行（1080p H264 → NV12）
./v4l2_dec_test test_1080p.h264 /tmp/out.nv12

# 预期: 57帧 1080p NV12 输出，每帧 3133440 字节
```

### V4L2 M2M 解码流程（Venus 特有）

```
1. S_FMT OUTPUT    (H264, width, height)
2. SUBSCRIBE_EVENT (SOURCE_CHANGE)
3. REQBUFS OUTPUT  (4 bufs, MMAP)
4. STREAMON OUTPUT
5. QBUF OUTPUT     (压缩数据 + KEYFRAME flag)
6. poll(POLLPRI) → 等待 SOURCE_CHANGE 事件
7. G_FMT CAPTURE   (获取检测到的分辨率)
8. S_FMT CAPTURE    (NV12)
9. REQBUFS CAPTURE  (16 bufs, MMAP)
10. QBUF CAPTURE ×N (入队所有 capture buffer)
11. STREAMON CAPTURE
12. poll(POLLIN) → DQBUF CAPTURE (解码帧)
13. QBUF CAPTURE   (重新入队)
```

**注意**: Venus 驱动默认输出 Q08C（高通压缩格式），需显式 S_FMT 设置 NV12。

## GStreamer 硬解（VAAPI 路径，已实测可用）

**先确认插件没有混版**，这是本镜像最大的坑：trixie 装的是 GStreamer 1.26.2
（`/usr/lib/aarch64-linux-gnu/gstreamer-1.0/`，275 个），而 BSP overlay 又在
`/usr/lib/gstreamer-1.0/` 铺了一整套 **1.22.12** 的插件（224 个，里面除 qti* 之外
有 186 个与 trixie 同名）。`GST_PLUGIN_PATH` 必须把 trixie 目录**排在前面**，否则
1.22.12 的 qtdemux 先注册，和 1.26 的核心协商不上，任何 playbin/decodebin 型
播放器（含 stock totem）都报 `qtdemux0: Internal data stream error /
not-negotiated`：

```bash
GST_PLUGIN_PATH=/usr/lib/aarch64-linux-gnu/gstreamer-1.0:/usr/lib/gstreamer-1.0
```

这样 qti/NPU 那 35 个插件照样注册（只调顺序，不删文件；rsync overlay 也删不掉
文件）。自查：`gst-launch-1.0 --version` 打印的二进制是 1.22.12 而 `GStreamer`
一行是 1.26.2，这个"二进制旧、库新"的组合本身就是 BSP 覆盖 `/usr/bin` 的痕迹。

**v4l2h264dec 在本镜像上不可用**（`Setup the capture queue` → `unsupported pixel
format`，或 `STREAMON 12 (Cannot allocate memory)`），别再用它。可用路径是 va
插件的 `vah264dec`，它要 `GST_VA_ALL_DRIVERS=1`（见下）才会注册：

```bash
# 上屏（HDMI 桌面；必须以桌面用户跑，不能 root）
gst-launch-1.0 playbin uri=file:///path/test.mp4

# 只验证硬解通路
gst-launch-1.0 filesrc location=test.h264 ! h264parse ! vah264dec ! fakesink
```

即使 va 插件注册了，`vah264dec` 和 `v4l2h264dec` 也是**并列 PRIMARY+1**，按注册
顺序 `v4l2h264dec` 赢，于是 decodebin 会挑到那个坏掉的。必须显式打破平局：

```bash
GST_PLUGIN_FEATURE_RANK=vah264dec:MAX
```

实测（QCS6490, 6.6.116-qli-1.7-ver.1.1, stock totem）：`sessions=1`，FBD 从 0 线性
涨到 246（240 帧的 4K30 视频，≈31 fps 实时），全程无报错，四象限测试片颜色全对
（`#ff1800` / `#ffffff` / `#000fff` / `#fff000`），连拍两帧像素不同（画面在动）。
sink 侧只有 `gtk4paintablesink` 能接上 vah264dec，`glimagesink`/`waylandsink`/
`videoconvert` 都会 not-negotiated。

以上四个变量都固化在 `prebuild/bsp-fix/etc/environment`（板端 `/etc/environment`），
由 pam_env 提供，GUI 会话和 `su -` 登录 shell 都能拿到，**不需要在命令行重复传**。

**不要**在 filesrc 后加 `video/x-h264,stream-format=byte-stream,alignment=au` 这类
capsfilter：会和 h264parse 的输出 caps 冲突，报
`h264parse0: Internal data stream error / not-negotiated`。

## 常见问题

### 0. 测任何东西之前先重置 VPU

被 kill 掉的播放器会**泄漏 msm_vidc 会话**，残留会话会让之后的解码器 `FBD` 卡在
0 或让 STREAMON 报 `Cannot allocate memory`，于是你看到的"新失败"其实是上一次
测试的污染。每轮测试前先：

```bash
echo aa00000.video-codec > /sys/bus/platform/drivers/msm_vidc_v4l2/unbind; sleep 2
echo aa00000.video-codec > /sys/bus/platform/drivers/msm_vidc_v4l2/bind; sleep 3
ls -d /sys/kernel/debug/msm_vidc/core/inst_* 2>/dev/null | wc -l   # 应为 0
```

`FBD` 计数在 `/sys/kernel/debug/msm_vidc/core/inst_*/info`，播放中应线性增长
（4K30 ≈ 31/s；到 240 后归零说明 8s 片子播完/循环），这是"解码真的在推进"的判据。

**判"画面上屏"要先自检素材和仪器**（踩过坑）：

1. **素材得真的会动**。`quad4k.mp4` 的四色象限是**逐帧完全相同**的静态图
   （生成时 `drawbox` 的 `t` 表达式没生效），拿它做"画面有没有动"的测试必然误判。
   自检：`ffmpeg -i clip.mp4 -vf scale=8:8,format=rgb24 -f rawvideo - | ...` 数不同帧数。
   要动态素材就用 2s 一段的整屏换色（红→绿→蓝→黄），截图看整屏主色变没变。
2. **截图工具有效性要先校准**。`gnome-screenshot` 在本板是可信的：先改壁纸红→绿
   截图验证签名确实变了，再拿它判播放画面。`ffmpeg -f kmsgrab`（返回 0 字节）和
   读 `/dev/fb0`（整屏只有一个值，不是活扫描输出）在本板**都不可用**，别浪费时间去试。
3. 截图比较**要看像素**：`gnome-screenshot` 的 PNG 有元数据，md5 不同不代表画面不同。
   用 `cmp` 比原始像素，或取 `crop=1:1` 采样。

### 1. "Cannot allocate memory" 打开 /dev/video32

原因: 4K 会话额度用尽（`MAX_NUM_4K_SESSIONS = 2`，被僵尸 CLOSE/ERROR 会话占掉）
      或 VPU 固件崩溃。见上面「VPU 会话泄漏修复」。
      **根因（关闭路径不停流 + 队列引用环）已由补丁治住 —— 见「治本」一节；
      打了补丁的固件上正常杀播放器不再留死会话。**
解决: unbind/rebind 重置 VPU，或重启设备。

### 2. "inst in error state"

原因: 发送了无效的码流数据，VPU 实例进入错误状态。
解决: 关闭 fd 重新打开，或 unbind/rebind。

### 3. "Power on failed" / "core not in valid state"

原因: VPU 固件崩溃，unbind/rebind 无法恢复。
解决: 必须重启设备 `adb shell reboot`。

### 4. ffmpeg 卡在 header 不输出

原因: `adb shell` 的 stdin 指向 tty，ffmpeg 修改终端模式时收到 SIGTTOU 被挂起。
解决: stdin 重定向 ` < /dev/null`。

### 5. vainfo 显示 "va_openDriver() returns -1"

原因: 驱动 .so 文件缺失或加载失败。
解决: 确认 `msm_drv_video.so` 在 `/usr/lib/aarch64-linux-gnu/dri/`。

注意排查顺序：先 `ls` 那个 .so 是否真的存在。装驱动时若磁盘满（板子 `/tmp` 是
3.6G tmpfs，很容易被 raw NV12 测试文件写满），`cp/install` 会失败而旧文件已被删掉，
表现就是 vainfo 突然打不开驱动。

### 6. mpv --hwdec=v4l2m2m-copy 全是坏帧

原因: FFmpeg 的 v4l2m2m 解码器只对 capture 端做 G_FMT、不做 S_FMT，于是拿到驱动的
默认 Q08C（UBWC 压缩）却按 yuv420p 解释。
验证: `ffmpeg -v debug -c:v h264_v4l2m2m ...` 会打印
`requesting formats: output=H264/none capture=Q08C/yuv420p` 然后
`An invalid frame was output by a decoder`。

FFmpeg 没有暴露 capture 格式选项（`ffmpeg -h decoder=h264_v4l2m2m` 只有
`num_output_buffers` / `num_capture_buffers`），所以改不了。用 GStreamer 那条路
（见上文，插件会自己协商 NV12），或改内核平台默认 capture 格式。

### 7. VLC 绿屏 / 用不上硬解

**已修复（2026-09-21，fork commit `766f935`）。** 症状是：VLC 窗口正常、进度条在走、
VPU 计数器在涨，但画面是一整块纯绿，啥也看不到。

两个根因，都在我们的驱动里，且都只在 VLC 这条路径上暴露：

1. **`vaAcquireBufferHandle`/`vaReleaseBufferHandle` 是 stub**（`va_stubs.c` 直接返回
   `VA_STATUS_ERROR_INVALID_BUFFER`）。VLC 的 GL interop 在显示第一帧之前，会先对
   surface 池做探测：`vaDeriveImage` + `vaAcquireBufferHandle`（源码
   `modules/video_output/opengl/converter_vaapi.c` 的 `tc_vaegl_get_pool`），探测失败
   就整池放弃 → 回落软解。
   **判据**：`VENUS_VAAPI_LOG=1 vlc ...` 里有
   `glconv_vaapi_wl gl error: vaAcquireBufferHandle: invalid VABufferID`。
2. **`vaExportSurfaceHandle` 的 layer 布局**：VLC 每帧调它时 `flags` 传 0，而 VLC 拿到
   descriptor 后会拒绝任何 `num_planes > 1` 的 layer（同一文件里的 `tc_vaegl_update`）。
   驱动原先无论 flags 都返回「1 层 2 平面」的 composed NV12 → VLC 每帧 `goto error`，
   纹理从来没被填过，而空的 YUV 纹理渲染出来就是**纯绿**（Y=0,U=V=0 → RGB≈(0,135,0)）。
   **判据**：日志里 export 次数正常（几百次）、`gl error` 为 0，但画面绿；绿屏截图体积很小
   （纯色 → PNG 只有 ~10KB，正常画面 1.7MB+）。
   **修法**：只有显式 `VA_EXPORT_SURFACE_COMPOSED_LAYERS` 才返回 composed，否则一层一平面
   （R8 + GR88）。mpv/ffmpeg 不受影响（它们本来就按自己的方式解析）。

其它容易踩的坑：

- VLC 需要「能给它窗口的界面」才能建 vout：`-I dummy` 会全部失败
  （`gl/gles2/xcb vout display error: parent window not available`）。Debian 镜像上
  Qt 走 xcb/XWayland（`libqxcb.so` 自带）就够了；装 `qtwayland5` 则走 Wayland 原生。
  两者都验证过能显示。
- 起 VLC 要带 `XDG_RUNTIME_DIR` + `WAYLAND_DISPLAY` + `DBUS_SESSION_BUS_ADDRESS`；
  走 xcb 还要 `DISPLAY` + `XAUTHORITY`（GNOME 的 cookie 在
  `/run/user/1001/.mutter-Xwaylandauth.*`，不在 `~/.Xauthority`）。
- 上一个 VLC 没退干净会让下一个拿不到窗口（报 `no suitable interface module` +
  退回 dummy interface）。
- 回归测试：`files/tests/vlc_display.sh`（`verify.sh` 第 6 项）用 2x2 纯色片截图并采样色块，
  绿屏必然判 FAIL。构造坏驱动的验证：把
  `objects.c` 里 `composed = (flags & VA_EXPORT_SURFACE_COMPOSED_LAYERS) != 0;`
  改成 `composed = true;` 重编，该测试就会失败（已实测）。

VLC 仍不可用时用 mpv 或 GStreamer 上屏（见上文）。

## 源码索引

| 文件 | 说明 |
|------|------|
| `layers/meta-quectel/recipes-multimedia/msm-va-driver/files/src/` | VA-API 驱动源码（上游 + 本项目适配，fork 分支的本地副本） |
| `layers/meta-quectel/recipes-multimedia/msm-va-driver/qcm6490-msm_vidc-adapt.patch` | 对上游 `47744788` 的完整改动 patch（review 用，recipe 不 apply） |
| `layers/meta-quectel/recipes-multimedia/msm-va-driver/build.sh` | 板子上编译/安装 |
| `layers/meta-quectel/recipes-multimedia/msm-va-driver/verify.sh` | 从仓库源码重建并做硬解/软解逐字节比对、mpv/VLC 上屏比对 |
| `layers/meta-quectel/recipes-multimedia/msm-va-driver/files/tests/vlc_display.sh` | VLC 上屏回归测试（绿屏判 FAIL） |
| `layers/meta-quectel/recipes-multimedia/msm-va-driver/msm-va-driver_1.0.bb` | 进镜像的 recipe（fork + 钉死 SRCREV，双路径安装） |
| `prebuild/gnome/etc/mpv/mpv.conf` | 固化的 mpv 播放配置（4K 走 dmabuf 直通上屏 + hwdec=vaapi-copy）。`gnome` 在 sync-list 里排 `bsp-fix` 之后，是 desktop 配置的最终话事层 |
| `layers/meta-quectel/recipes-multimedia/msm-va-driver/README.md` | 状态、限制、根因分析 |
| `sources/quectel-src/kernel/qcom-6.6/drivers/media/platform/qcom/venus/` | 上游 Venus 内核驱动（本板实际用的是厂商 `msm_vidc`，见下） |
| 厂商 `video-driver`（`iris_vpu.ko`） | 设备上 `modinfo iris_vpu` 可查；平台能力表在 `platform/qcm6490/src/msm_vidc_qcm6490.c`，`OUTPUT_ORDER` 默认 0（显示序输出）是 B 帧死锁的根源 |
| `/usr/include/va/va_backend.h` (设备) | VA-API 后端接口定义 |
