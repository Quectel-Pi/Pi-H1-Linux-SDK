# Camx V4L2 Bridge Requirements

## Overview
Bridge Qualcomm cam-server to v4l2loopback device, enabling standard V4L2 applications (cheese, OBS, etc.) to access Qualcomm cameras via `/dev/videoX`.

## Architecture
```
Real Camera → cam-server → bridge → v4l2loopback → /dev/videoX → Application
```

## Core Features

### 1. On-Demand Camera Activation (Non-Exclusive)
- Camera **only active when application opens device**
- Monitor device usage via `/proc/<pid>/fd` polling (500ms interval)
- Auto-start camera when readers detected
- Auto-stop camera when all readers closed
- **When no application accessing /dev/videoX, camera is released for other qmmf applications**
- Bridge does NOT hold camera resource when idle

### 2. Camera Resource Sharing
- **When no application accessing /dev/video10 or /dev/video11, bridge releases camera**
- Other qmmf applications (gst-launch qtiqmmfsrc, etc.) can access camera directly
- Bridge only holds camera resource during active streaming
- No conflict with other camera applications when bridge is idle
- Bridge maintains cam-server connection but releases camera hardware
- **Implementation**: `deactivate_camera()` calls `stop_camera()` which releases camera resource

### 3. Multi-Instance Support
- Template service `camx-v4l2-bridge@.service` supports multiple cameras
- Instance `@0` → Camera 0 → `/dev/video10`
- Instance `@1` → Camera 1 → `/dev/video11`
- Each instance runs independently
- Each instance manages its own camera resource independently

### 4. Optional Camera Hardware
- Support scenarios where cameras may not be plugged in
- Probe camera with **up to 3 retries** (2s interval) to handle transient detection failures
- Exit with code **2** when camera not found after retries (service will not restart)
- Service configured with `RestartPreventExitStatus=2`

### 5. Frame Format
- Input: NV12 from cam-server
- Output: NV12 to v4l2loopback
- Handle stride mismatch (repack frame if needed)
- Write black frame when idle to maintain capture capabilities

## Exit Codes

| Code | Meaning | systemd Action |
|---|---|---|
| `0` | Clean shutdown (SIGTERM/SIGINT) | No restart |
| `1` | Runtime failure (cam-server died, StartCamera failed, cam-server not ready) | **Restart** via `Restart=on-failure` |
| `2` | Camera hardware not present | No restart via `RestartPreventExitStatus=2` |

This exit code scheme enables systemd to automatically retry when cam-server is not ready at boot,
while preventing restart loops when camera hardware is genuinely absent.

## Service Configuration

### v4l2loopback-setup.service
```ini
ExecStart=/sbin/modprobe v4l2loopback video_nr=10,11
```

### camx-v4l2-bridge@.service
```ini
[Unit]
After=cam-server.service v4l2loopback-setup.service
Requires=cam-server.service

[Service]
ExecStartPre=/bin/sleep 8
ExecStart=/usr/bin/camx-v4l2-bridge -c %i -d /dev/video1%i -w 1280 -h 720 -f 30
Restart=on-failure
RestartSec=5
RestartPreventExitStatus=2
```

## Usage Examples

### Start single camera
```bash
systemctl start camx-v4l2-bridge@0
```

### Start both cameras
```bash
systemctl start camx-v4l2-bridge@0
systemctl start camx-v4l2-bridge@1
```

### Manual testing
```bash
# Test camera 0
camx-v4l2-bridge -c 0 -d /dev/video10 -w 1920 -h 1080

# Test camera 1
camx-v4l2-bridge -c 1 -d /dev/video11 -w 1920 -h 1080
```

## Dependencies
- `v4l2loopback` kernel module
- `cam-server` service running
- `qmmf_recorder_client` library
- `qmmf_camera_metadata` library

## Limitations
- Cannot run simultaneously with GStreamer qtiqmmfsrc on same camera
- Device monitoring relies on /proc polling (500ms latency)
- Camera ID must match cam-server's camera enumeration

## Testing & Debugging

### Manual Testing

```bash
# 1. Stop service first
sudo systemctl stop camx-v4l2-bridge@0

# 2. Run manually to see output
sudo /usr/bin/camx-v4l2-bridge -c 0 -d /dev/video10 -w 1280 -h 720 -f 30

# 3. Test camera detection (should return 2 if no camera)
sudo /usr/bin/camx-v4l2-bridge -c 99 -d /dev/video10 -w 1280 -h 720
echo $?  # Should output 2

# 4. Test with cheese in another terminal
cheese
```

### Service Management

```bash
# Check service status
systemctl status camx-v4l2-bridge@0
systemctl status camx-v4l2-bridge@1

# View logs
journalctl -u camx-v4l2-bridge@0 --no-pager -n 30
journalctl -u camx-v4l2-bridge@0 -f  # Follow logs

# Enable/disable auto-start
sudo systemctl enable camx-v4l2-bridge@0
sudo systemctl disable camx-v4l2-bridge@0
```

### Debugging Commands

```bash
# Check processes
ps aux | grep camx-v4l2

# Check devices
ls -la /dev/video1*

# Check v4l2loopback status
v4l2-ctl --device=/dev/video10 --all
v4l2-ctl --device=/dev/video10 --list-formats

# Check cam-server
systemctl status cam-server
journalctl -u cam-server --no-pager -n 20

# Check v4l2loopback module
lsmod | grep v4l2loopback
cat /sys/module/v4l2loopback/parameters/exclusive_caps
```

### Common Issues & Solutions

#### 1. Bridge fails to connect to cam-server
```bash
# Check cam-server is running
systemctl status cam-server

# Restart cam-server
sudo systemctl restart cam-server

# Wait for cam-server to stabilize
sleep 5

# Then restart bridge
sudo systemctl restart camx-v4l2-bridge@0
```

#### 2. Camera not detected (exit code 2)
```bash
# Check if camera hardware is present
# Try different camera IDs
sudo /usr/bin/camx-v4l2-bridge -c 0 -d /dev/video10 -w 1280 -h 720
sudo /usr/bin/camx-v4l2-bridge -c 1 -d /dev/video10 -w 1280 -h 720

# Check cam-server logs for camera info
journalctl -u cam-server | grep -i camera
```

#### 3. Cheese doesn't see camera
```bash
# Check v4l2loopback device exists
ls -la /dev/video10

# Check device capabilities
v4l2-ctl --device=/dev/video10 --list-formats

# Restart bridge to write initial black frame
sudo systemctl restart camx-v4l2-bridge@0
```

#### 4. GStreamer conflict
```bash
# Kill all GStreamer processes
sudo pkill -f gst-launch
sudo pkill -f qtiqmmfsrc

# Restart cam-server
sudo systemctl restart cam-server

# Restart bridge
sudo systemctl restart camx-v4l2-bridge@0
```

### Verification Steps

```bash
# 1. Check v4l2loopback module loaded
lsmod | grep v4l2loopback

# 2. Check devices exist
ls -la /dev/video10 /dev/video11

# 3. Check services running
systemctl status camx-v4l2-bridge@0
systemctl status camx-v4l2-bridge@1

# 4. Test camera access
cheese  # Should show camera feed

# 5. Test camera release
killall cheese
sleep 1
# Camera should be released for other qmmf apps
gst-launch-1.0 qtiqmmfsrc camera-id=0 ! fakesink  # Should work
```

## Lessons Learned

### 1. Camera ID Mapping
- GStreamer qtiqmmfsrc `camera-id=0` may not match Recorder API `camera_id=0`
- Always test with actual Recorder API to find correct camera ID

### 2. cam-server Connection Stability
- cam-server may disconnect clients that don't call API immediately
- Call `GetCamStaticInfo()` right after `Connect()` to stabilize connection
- `connect_cam_server()` retries up to **30 times** (2s interval) to handle cam-server startup delay
- Add `ExecStartPre=/bin/sleep 8` to wait for cam-server stability
- **Exit on failure, let systemd restart**: when `kServerDied` or `StartCamera` fails, bridge exits with code 1
- systemd `Restart=on-failure` automatically restarts the service (with `RestartSec=5` backoff)
- This handles cam-server startup delay: bridge retries via systemd restart until cam-server is ready
- Camera not found → exit code 2 → `RestartPreventExitStatus=2` stops restart loop

### 3. Probe Camera with Limited Retry
- Camera is optional hardware, may not be connected
- Probe with up to **3 retries** (2s interval) to handle transient detection failures
- Return exit code 2 to prevent service restart loop

### 4. v4l2loopback Device States
- Device exists but may not have capture caps until first frame written
- Write black frame on startup to expose capture capabilities
- Use `exclusive_caps=1` for better application compatibility

### 5. Resource Sharing Pattern
- Bridge should NOT hold camera when idle
- Release camera when no readers detected
- Other qmmf apps can access camera when bridge is idle
- Monitor `/proc/<pid>/fd` to detect device users

### 6. Frame Handling
- Frame queue capped at 4 frames; oldest frames dropped when queue full
- Always repack frames to handle stride mismatch between cam-server and v4l2loopback
- Y plane and UV plane copied row-by-row with correct stride alignment
- v4l2 write thread handles EAGAIN/EINTR/ENODATA gracefully (ENODATA is normal when no reader)
