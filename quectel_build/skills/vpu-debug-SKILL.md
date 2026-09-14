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

本项目自带 VA-API 兼容层，将 VA-API 解码请求转换为 Venus V4L2 M2M 调用。

源码: `layers/meta-quectel/recipes-multimedia/msm-va-driver/files/msm_drv_video.c`
安装位置: `/usr/lib/aarch64-linux-gnu/dri/msm_drv_video.so`

### 使用方法

```bash
# 设置环境变量
export LIBVA_DRIVER_NAME=msm

# vainfo 验证
vainfo

# ffmpeg 硬件解码（必须加 -hwaccel_output_format vaapi，且 stdin 重定向）
ffmpeg -hwaccel vaapi -hwaccel_output_format vaapi \
  -hwaccel_device /dev/dri/renderD128 \
  -i input.h264 -f null - < /dev/null

# mpv 硬件解码播放
mpv --hwdec=vaapi --vo=null video.mp4
```

### VA-API 解码验证

```bash
# 编译测试程序
gcc -o va_test va_test.c -lva -lva-drm

# 运行测试
export LIBVA_DRIVER_NAME=msm
./va_test test.h264 1920 1080 60

# 预期输出: "DECODE VERIFIED: NV12 data is valid"
```

### VA-API 限制

- 只支持解码（VAEntrypointVLD），不支持编码
- 输出格式: NV12 only（CPU 内存拷贝，非 DMABUF 零拷贝）
- ffmpeg 不带 `-hwaccel_output_format vaapi` 时帧下载路径不完整
- mpv 完全支持（走 DeriveImage 路径）

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

## GStreamer 硬解

设备上有 v4l2h264dec 插件，但 Venus 默认输出 Q08C 导致 GStreamer 报错:
```
ERROR: V4L2 format Q08C not supported
```

解决方法: 在 pipeline 中强制 NV12 输出:
```bash
gst-launch-1.0 filesrc location=test.h264 ! h264parse ! \
  v4l2h264dec capture-io-mode=dmabuf ! \
  video/x-raw,format=NV12 ! fakesink
```

但实际可能仍不工作，因为 GStreamer v4l2 插件不支持 Q08C 自动协商到 NV12。

## 常见问题

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

## 源码索引

| 文件 | 说明 |
|------|------|
| `layers/meta-quectel/recipes-multimedia/msm-va-driver/files/msm_drv_video.c` | VA-API 兼容层驱动源码 |
| `layers/meta-quectel/recipes-multimedia/msm-va-driver/msm-va-driver_1.0.bb` | Yocto recipe |
| `sources/quectel-src/kernel/qcom-6.6/drivers/media/platform/qcom/venus/` | Venus 内核驱动源码 |
| `/usr/include/va/va_backend.h` (设备) | VA-API 后端接口定义 |
