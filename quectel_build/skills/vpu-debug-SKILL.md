# VPU 视频解码调试 (Venus/iris_vpu)

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

VPU 固件有最大 16 会话限制，测试程序如未正确 STREAMOFF + close 会导致泄漏。

症状:
```
msm_vidc: err: msm_vidc_add_session: max limit 16 already running 16 sessions
msm_vidc: err: msm_vidc_open: failed to add session
```

修复方法（不需要重启设备）:
```bash
# 通过 sysfs unbind/rebind 重置 VPU
echo aa00000.video-codec > /sys/bus/platform/drivers/msm_vidc_v4l2/unbind
sleep 2
echo aa00000.video-codec > /sys/bus/platform/drivers/msm_vidc_v4l2/bind
sleep 3

# 验证恢复
v4l2-ctl -d /dev/video32 --all | head -3
```

如果 unbind/rebind 仍无法恢复（"Power on failed"），需要重启设备:
```bash
adb shell reboot   # 注意：用 adb shell reboot，不要用 adb reboot
```

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

**结论 3：4K 掉帧是上屏（合成器）问题，不是解码。** `mpv --hwdec=vaapi` 掉 247-258/617，
`--hwdec=vaapi-copy` 只掉 100-103，两者都在 21.7s 内放完（片长 20.6s）→ 解码有 2.4x 余量。
4K 播放优先 `--hwdec=vaapi-copy`。

### 上屏路径才是掉帧来源，已固化 mpv 配置

4K 播放掉帧与解码无关（dec=0），全在上屏：同一个 4K30 片 300 帧（10.0s 内容），
交替 A/B 四轮：

| 上屏配置 | 掉帧 | wall |
|---------|------|------|
| `vo=dmabuf-wayland` + copy | **0, 1, 11** | 10.5s |
| `vo=dmabuf-wayland` + 直出(vaapi) | 32, 37, 35, 8 | 10.5s |
| `vo=gpu`(GL) 直出 | 95, 94, 98, 81 | 11.1s |
| `vo=gpu`(GL) + copy | 85, 89, 73 | 11.1s |

GPU 不是瓶颈（simple_ondemand 已把 GPU 拉到 550MHz 上限）。GL 路径在做 4K→1080p
缩放时掉帧，Wayland dmabuf 直通不掉。

固化位置（`prebuild/gnome/etc/mpv/mpv.conf`，`gnome` 在 `prebuild/sync-list` 里、
排在 `bsp-fix` 之后，所以它是 desktop 配置的最终话事层）：

```ini
hwdec=vaapi-copy

[large-frames]
profile-desc=DMABUF passthrough for >=1440p sources
profile-cond=width ~= nil and height ~= nil and (width >= 2560 or height >= 1440)
profile-restore=copy
vo=dmabuf-wayland,gpu
```

实测（无任何命令行参数，只靠这个配置）：

| 内容 | 掉帧 | 走的 vo |
|------|------|--------|
| 4K30 DJI | 0 / 2 / 0 | dmabuf-wayland |
| 4K30 带 B 帧 | 12 / 1（不再卡死） | dmabuf-wayland |
| 4K60 | ~180 | dmabuf-wayland（VPU 到顶，见上） |
| 1080p | 0 / 1 / 0 | gpu（保留字幕/OSD） |

两个坑：
- **`profile-cond` 必须判 nil**。`width`/`height` 在流信息就绪前是 nil，直接比较会抛
  Lua 错误，mpv 报 `Errors when loading file` 直接中止播放（4K60 就这样挂过）。
- **mpv 默认 `hwdec=no`**，不写这个文件等于全程软解。
- 按分辨率分流的原因：`dmabuf-wayland` 只渲染视频，不支持 OSD/字幕；所以只在
  大分辨率时切过去，小分辨率留在 `vo=gpu`。

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

原因: VPU 会话数超限（16/16）或 VPU 固件崩溃。
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
