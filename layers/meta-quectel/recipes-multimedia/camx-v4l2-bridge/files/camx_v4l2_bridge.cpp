/*
 * Camx V4L2 Bridge - QCM6490
 *
 * Bridges Qualcomm cam-server to v4l2loopback device
 * so standard Debian applications can use /dev/videoX
 *
 * Camera is only active when an application opens the device.
 *
 * Build: aarch64-qcom-linux-g++ -O2 -std=c++17 camx_v4l2_bridge.cpp \
 *        -o camx-v4l2-bridge -lqmmf_recorder_client -lpthread
 *
 * Usage: modprobe v4l2loopback video_nr=10 exclusive_caps=1
 *        ./camx-v4l2-bridge -c 0 -d /dev/video10 -w 1280 -h 720
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <vector>

#include <linux/videodev2.h>

/* cam-server API */
#include <qmmf-sdk/qmmf_recorder.h>
#include <qmmf-sdk/qmmf_recorder_params.h>
#include <qmmf-sdk/qmmf_buffer.h>

/* ------------------------------------------------------------------ */
/* Global state                                                        */
/* ------------------------------------------------------------------ */

static std::atomic<bool> g_running{true};
static std::atomic<bool> g_camera_active{false};
static std::atomic<bool> g_streaming{false};
static std::atomic<bool> g_abnormal_exit{false};
static std::atomic<bool> g_signal_shutdown{false};
static std::atomic<int> g_skip_frames{0};  /* discard first N frames after camera start */

static int g_camera_id = 0;
static int g_v4l2_fd = -1;
static int g_width = 1280;
static int g_height = 720;
static float g_fps = 30.0f;
static uint32_t g_track_id = 1;

static qmmf::recorder::Recorder *g_recorder = nullptr;
static const char *g_device_path = "/dev/video10";

/* Frame queue */
struct FrameData {
    std::vector<uint8_t> data;
    uint64_t timestamp;
    uint32_t stride;
    uint32_t scanline;
};

static std::mutex g_frame_mutex;
static std::condition_variable g_frame_cv;
static std::queue<FrameData> g_frame_queue;
static const size_t MAX_QUEUE_SIZE = 4;

/* Reader tracking */
static std::atomic<int> g_reader_count{0};

/* ------------------------------------------------------------------ */
/* Signal handler                                                      */
/* ------------------------------------------------------------------ */

static void signal_handler(int sig)
{
    (void)sig;
    g_running = false;
    g_signal_shutdown = true;
    g_frame_cv.notify_all();
}

/* ------------------------------------------------------------------ */
/* V4L2 loopback setup                                                 */
/* ------------------------------------------------------------------ */

static int setup_v4l2loopback(const char *device, int width, int height)
{
    int fd = open(device, O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        fprintf(stderr, "Error: Cannot open %s: %s\n",
                device, strerror(errno));
        return -1;
    }

    struct v4l2_format fmt = {};
    fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    fmt.fmt.pix.width = width;
    fmt.fmt.pix.height = height;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_NV12;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    fmt.fmt.pix.bytesperline = width;
    fmt.fmt.pix.sizeimage = width * height * 3 / 2;

    if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
        fprintf(stderr, "Error: VIDIOC_S_FMT failed: %s\n",
                strerror(errno));
        close(fd);
        return -1;
    }

    printf("V4L2 loopback configured: %s (%dx%d NV12)\n",
           device, width, height);
    return fd;
}

/* ------------------------------------------------------------------ */
/* cam-server callbacks                                                */
/* ------------------------------------------------------------------ */

static void on_recorder_event(qmmf::recorder::EventType type,
                              void *data, size_t size)
{
    (void)data;
    (void)size;

    switch (type) {
    case qmmf::recorder::EventType::kCameraError:
        fprintf(stderr, "Warning: Camera error occurred\n");
        break;
    case qmmf::recorder::EventType::kServerDied:
        fprintf(stderr, "Error: cam-server died!\n");
        g_abnormal_exit = true;
        g_running = false;
        g_frame_cv.notify_all();
        break;
    case qmmf::recorder::EventType::kFatal:
        fprintf(stderr, "Error: Fatal camera error!\n");
        g_running = false;
        break;
    default:
        break;
    }
}

static void on_track_data(uint32_t track_id,
                          std::vector<qmmf::BufferDescriptor> buffers,
                          std::vector<qmmf::BufferMeta> metas)
{
    if (buffers.empty() || !g_streaming)
        return;

    /* Skip initial frames after camera start - ISP pipeline needs time to
     * warm up and produce valid frames. Without this, the first camera app
     * open gets black/invalid frames and shows a black screen. */
    int remaining = g_skip_frames.load();
    if (remaining > 0) {
        g_skip_frames.store(remaining - 1);
        g_recorder->ReturnTrackBuffer(track_id, buffers);
        if (remaining == 1)
            printf("ISP warm-up complete, frame delivery started\n");
        return;
    }

    const auto &buf = buffers[0];

    /* Get stride from metadata */
    uint32_t stride = g_width;
    uint32_t scanline = g_height;
    if (!metas.empty() && metas[0].n_planes > 0) {
        stride = metas[0].planes[0].stride;
        scanline = metas[0].planes[0].scanline;
    }

    /* Copy frame to queue */
    {
        std::lock_guard<std::mutex> lock(g_frame_mutex);

        while (g_frame_queue.size() >= MAX_QUEUE_SIZE) {
            g_frame_queue.pop();
        }

        FrameData frame;
        /* Skip metadata if offset > 0 */
        uint8_t *frame_start = static_cast<uint8_t *>(buf.data) + buf.offset;
        uint32_t frame_size = buf.size - buf.offset;

        /* Debug: print offset info (only first frame) */
        static bool first_frame = true;
        if (first_frame) {
            printf("Buffer info: total=%u, offset=%u, frame_size=%u, stride=%u, scanline=%u\n",
                   buf.size, buf.offset, frame_size, stride, scanline);
            first_frame = false;
        }

        frame.data.assign(frame_start, frame_start + frame_size);
        frame.timestamp = buf.timestamp;
        frame.stride = stride;
        frame.scanline = scanline;
        g_frame_queue.push(std::move(frame));
    }
    g_frame_cv.notify_one();

    /* Return buffer to cam-server */
    g_recorder->ReturnTrackBuffer(track_id, buffers);
}

static void on_track_event(uint32_t track_id,
                           qmmf::recorder::EventType type,
                           void *payload, size_t size)
{
    (void)track_id;
    (void)payload;
    (void)size;
}

/* ------------------------------------------------------------------ */
/* cam-server operations                                               */
/* ------------------------------------------------------------------ */

static int connect_cam_server()
{
    const int max_retries = 30;
    const int retry_interval_sec = 2;

    for (int attempt = 1; attempt <= max_retries; attempt++) {
        g_recorder = new qmmf::recorder::Recorder();

        qmmf::recorder::RecorderCb cbs;
        cbs.event_cb = on_recorder_event;

        qmmf::recorder::status_t ret = g_recorder->Connect(cbs);
        if (ret == 0) {
            printf("Connected to cam-server (attempt %d)\n", attempt);
            /*
             * Do NOT call GetCamStaticInfo() here. It triggers a second
             * RegisterClient on cam-server, which logs "Client is already
             * connected !!". cam-server then treats the client as dead
             * (~4 s later) and force-cleans RecorderImpl, after which
             * StartCamera() permanently fails with "Recorder not
             * initialized!" until the whole process restarts. The camera
             * count is probed once in probe_camera() instead.
             */
            return 0;
        }

        fprintf(stderr, "Retry %d/%d: Failed to connect to cam-server: %d\n",
                attempt, max_retries, ret);
        delete g_recorder;
        g_recorder = nullptr;

        if (attempt < max_retries)
            sleep(retry_interval_sec);
    }

    fprintf(stderr, "Error: Failed to connect to cam-server after %d attempts\n",
            max_retries);
    return -1;
}

static void disconnect_cam_server()
{
    if (!g_recorder)
        return;

    g_recorder->Disconnect();
    delete g_recorder;
    g_recorder = nullptr;
    printf("Disconnected from cam-server\n");
}

/* Probe if the camera hardware exists (with retry) */
static int probe_camera()
{
    const int max_retries = 3;
    const int retry_delay_sec = 2;

    for (int attempt = 1; attempt <= max_retries; attempt++) {
        std::vector<qmmf::CameraMetadata> cam_list;
        qmmf::recorder::status_t ret;

        ret = g_recorder->GetCamStaticInfo(cam_list);
        if (ret != 0) {
            fprintf(stderr, "Warning: GetCamStaticInfo failed: %d (attempt %d/%d)\n",
                    ret, attempt, max_retries);
            if (attempt < max_retries)
                sleep(retry_delay_sec);
            continue;
        }

        printf("Found %zu camera(s) in system\n", cam_list.size());

        if ((int)cam_list.size() > g_camera_id) {
            printf("Camera %d detected\n", g_camera_id);
            return 0;  /* Camera found */
        }

        fprintf(stderr, "Warning: Camera %d not found (only %zu camera(s) available) (attempt %d/%d)\n",
                g_camera_id, cam_list.size(), attempt, max_retries);

        if (attempt < max_retries)
            sleep(retry_delay_sec);
    }

    fprintf(stderr, "Error: Camera %d not found after %d attempts, exiting.\n",
            g_camera_id, max_retries);
    return 2;  /* Exit code 2 = no camera, don't restart */
}

static int start_camera()
{
    if (g_camera_active)
        return 0;

    qmmf::recorder::CameraExtraParam xtraparam;
    qmmf::recorder::status_t ret;

    /*
     * On cold boot, cam-server's RecorderImpl may be torn down by idle
     * cleanup after the initial Connect(). StartCamera() then returns
     * -EINVAL (-22) with cam-server logging "Recorder not initialized!".
     * Do a couple of quick retries in case it's a transient race; if it
     * still fails, return -1 so activate_camera() can disconnect/reconnect
     * to force cam-server to recreate RecorderImpl — avoids the slow
     * systemd restart cycle (RestartSec=5 + ExecStartPre sleep 8 + reconnect
     * ≈ 15 s of black screen on first camera open).
     */
    const int max_retries = 2;
    const int retry_interval_us = 200000;  /* 0.2 s */

    for (int attempt = 1; attempt <= max_retries; attempt++) {
        ret = g_recorder->StartCamera(g_camera_id, g_fps, xtraparam);
        if (ret == 0)
            break;

        fprintf(stderr, "Warning: StartCamera failed: %d (attempt %d/%d)\n",
                ret, attempt, max_retries);

        if (attempt < max_retries)
            usleep(retry_interval_us);
    }

    if (ret != 0) {
        fprintf(stderr, "Error: StartCamera failed: %d (RecorderImpl may be "
                "torn down, caller should reconnect)\n", ret);
        return -1;
    }

    g_camera_active = true;
    printf("Camera %d started\n", g_camera_id);
    return 0;
}

static void stop_camera()
{
    if (!g_camera_active)
        return;

    g_camera_active = false;
    g_recorder->StopCamera(g_camera_id);
    printf("Camera %d stopped\n", g_camera_id);
}

static int start_streaming()
{
    if (g_streaming)
        return 0;

    qmmf::recorder::VideoTrackParam param;
    param.camera_id = g_camera_id;
    param.width = g_width;
    param.height = g_height;
    param.framerate = g_fps;
    param.format = qmmf::recorder::VideoFormat::kNV12;

    qmmf::recorder::VideoExtraParam xtraparam;

    qmmf::recorder::TrackCb track_cb;
    track_cb.data_cb = [](uint32_t track_id,
                          std::vector<qmmf::BufferDescriptor> buffers,
                          std::vector<qmmf::BufferMeta> metas) {
        on_track_data(track_id, std::move(buffers), std::move(metas));
    };
    track_cb.event_cb = [](uint32_t track_id,
                           qmmf::recorder::EventType type,
                           void *payload, size_t size) {
        on_track_event(track_id, type, payload, size);
    };

    qmmf::recorder::status_t ret;

    /*
     * Set g_streaming BEFORE CreateVideoTrack — the callback is registered
     * during creation and cam-server may start delivering frames immediately.
     * If g_streaming is still false at that point, on_track_data() drops
     * every frame and the first camera-app open never receives an image.
     */
    g_streaming = true;
    g_skip_frames.store(5);  /* discard first 5 frames while ISP warms up */

    ret = g_recorder->CreateVideoTrack(g_track_id, param, xtraparam, track_cb);
    if (ret != 0) {
        fprintf(stderr, "Error: CreateVideoTrack failed: %d\n", ret);
        g_streaming = false;
        return -1;
    }

    std::unordered_set<uint32_t> track_ids = {g_track_id};
    ret = g_recorder->StartVideoTracks(track_ids);
    if (ret != 0) {
        fprintf(stderr, "Error: StartVideoTracks failed: %d\n", ret);
        g_recorder->DeleteVideoTrack(g_track_id);
        g_streaming = false;
        return -1;
    }

    printf("Streaming started (%dx%d @%.0f NV12)\n",
           g_width, g_height, g_fps);
    return 0;
}

static void stop_streaming()
{
    if (!g_streaming)
        return;

    g_streaming = false;

    std::unordered_set<uint32_t> track_ids = {g_track_id};
    g_recorder->StopVideoTracks(track_ids);
    g_recorder->DeleteVideoTrack(g_track_id);

    /* Clear frame queue */
    {
        std::lock_guard<std::mutex> lock(g_frame_mutex);
        while (!g_frame_queue.empty())
            g_frame_queue.pop();
    }

    printf("Streaming stopped\n");
}

/* ------------------------------------------------------------------ */
/* Camera start/stop (called when readers appear/disappear)            */
/* ------------------------------------------------------------------ */

/*
 * Activate camera. Returns 0 on success, -1 on failure.
 * Caller should exit process on failure to let systemd restart.
 *
 * On cold boot, cam-server may have torn down its RecorderImpl shortly
 * after the initial Connect() (idle cleanup). StartCamera() then returns
 * -EINVAL with cam-server logging "Recorder not initialized!". Instead of
 * exiting and triggering a slow systemd restart cycle (~15 s black screen),
 * disconnect/reconnect cam-server in-place — this forces cam-server to
 * recreate RecorderImpl — then retry StartCamera + streaming.
 */
static int activate_camera()
{
    if (g_camera_active)
        return 0;

    printf("Reader detected, activating camera...\n");

    const int max_reconnect = 3;
    const int retry_delay_us = 500000;  /* 0.5 s */

    for (int attempt = 1; attempt <= max_reconnect; attempt++) {
        if (start_camera() == 0) {
            if (start_streaming() == 0)
                return 0;
            /* streaming failed — undo and retry below */
            stop_camera();
        }

        if (attempt >= max_reconnect)
            break;

        fprintf(stderr, "Warning: camera activate failed (attempt %d/%d), "
                "reconnecting to cam-server...\n", attempt, max_reconnect);

        /* Disconnect + reconnect forces cam-server to recreate
         * RecorderImpl, which was torn down by idle cleanup. */
        disconnect_cam_server();
        usleep(retry_delay_us);
        if (connect_cam_server() < 0) {
            fprintf(stderr, "Error: reconnect to cam-server failed\n");
            return -1;
        }
    }

    fprintf(stderr, "Error: Failed to activate camera after %d reconnect attempts, "
            "exiting to let systemd restart\n", max_reconnect);
    return -1;
}

/* Write a black frame to keep v4l2loopback capture caps exposed */
static void write_black_frame()
{
    if (g_v4l2_fd < 0)
        return;

    size_t y_size = g_width * g_height;
    size_t uv_size = g_width * g_height / 2;
    std::vector<uint8_t> frame(y_size + uv_size);
    memset(frame.data(), 0, y_size);
    memset(frame.data() + y_size, 0x80, uv_size);
    (void)write(g_v4l2_fd, frame.data(), frame.size());
}

static void deactivate_camera()
{
    if (!g_camera_active)
        return;

    printf("No readers, deactivating camera...\n");

    stop_streaming();
    stop_camera();

    /* Write black frame so device keeps capture caps exposed */
    write_black_frame();
}

/* ------------------------------------------------------------------ */
/* Device monitor thread (polling)                                     */
/* ------------------------------------------------------------------ */

/* Check if anyone has the device open by scanning /proc/<pid>/fd */
static int count_device_users(const char *device)
{
    DIR *proc_dir = opendir("/proc");
    if (!proc_dir)
        return -1;

    int count = 0;
    struct dirent *proc_entry;
    pid_t self = getpid();

    /* Get device major:minor */
    struct stat dev_stat;
    if (stat(device, &dev_stat) < 0) {
        closedir(proc_dir);
        return -1;
    }

    while ((proc_entry = readdir(proc_dir)) != NULL) {
        /* Skip non-numeric entries */
        if (proc_entry->d_name[0] < '0' || proc_entry->d_name[0] > '9')
            continue;

        /* Skip our own process */
        pid_t pid = atoi(proc_entry->d_name);
        if (pid == self)
            continue;

        char fd_path[256];
        snprintf(fd_path, sizeof(fd_path), "/proc/%s/fd", proc_entry->d_name);

        DIR *fd_dir = opendir(fd_path);
        if (!fd_dir)
            continue;

        struct dirent *fd_entry;
        while ((fd_entry = readdir(fd_dir)) != NULL) {
            if (fd_entry->d_name[0] < '0' || fd_entry->d_name[0] > '9')
                continue;

            char link_path[512];
            char link_target[256];
            snprintf(link_path, sizeof(link_path), "%s/%s",
                     fd_path, fd_entry->d_name);

            ssize_t len = readlink(link_path, link_target,
                                   sizeof(link_target) - 1);
            if (len > 0) {
                link_target[len] = '\0';
                if (strcmp(link_target, device) == 0) {
                    count++;
                }
            }
        }
        closedir(fd_dir);
    }

    closedir(proc_dir);
    return count;
}

static void *device_monitor_thread(void *arg)
{
    (void)arg;

    printf("Device monitor thread started (polling mode)\n");

    int was_active = 0;

    while (g_running) {
        /* Check if anyone has the device open */
        int users = count_device_users(g_device_path);

        if (users > 0 && !was_active) {
            /* Someone opened the device */
            printf("Device opened (users: %d), activating camera\n", users);
            if (activate_camera() < 0) {
                fprintf(stderr, "Error: Failed to activate camera, exiting "
                        "to let systemd restart\n");
                g_abnormal_exit = true;
                g_running = false;
                g_frame_cv.notify_all();
                return nullptr;
            }
            was_active = 1;
        } else if (users == 0 && was_active) {
            /* All users closed the device */
            printf("Device closed, deactivating camera\n");
            deactivate_camera();
            was_active = 0;
        }

        /* Poll every 200ms for faster camera activation on first open */
        usleep(200000);
    }

    printf("Device monitor thread stopped\n");
    return nullptr;
}

/* ------------------------------------------------------------------ */
/* V4L2 writer thread                                                  */
/* ------------------------------------------------------------------ */

static void *v4l2_writer_thread(void *arg)
{
    (void)arg;

    printf("V4L2 writer thread started\n");

    while (g_running) {
        FrameData frame;

        /* Wait for a frame */
        {
            std::unique_lock<std::mutex> lock(g_frame_mutex);
            g_frame_cv.wait_for(lock, std::chrono::milliseconds(500), [] {
                return !g_frame_queue.empty() || !g_running;
            });

            if (!g_running && g_frame_queue.empty())
                break;

            if (g_frame_queue.empty())
                continue;

            frame = std::move(g_frame_queue.front());
            g_frame_queue.pop();
        }

        /* Write frame to v4l2loopback */
        if (g_v4l2_fd >= 0 && !frame.data.empty() && g_streaming) {
            ssize_t written;
            size_t expected_size = g_width * g_height * 3 / 2;

            /* Always repack to ensure correct NV12 format */
            std::vector<uint8_t> repacked(expected_size);
            const uint8_t *src = frame.data.data();
            uint8_t *dst = repacked.data();

            /* Y plane: copy g_width bytes from each row */
            for (int y = 0; y < g_height; y++) {
                memcpy(dst + y * g_width,
                       src + y * frame.stride,
                       g_width);
            }

            /* UV plane: copy g_width bytes from each row */
            const uint8_t *src_uv = src + frame.stride * frame.scanline;
            uint8_t *dst_uv = dst + g_width * g_height;
            for (int y = 0; y < g_height / 2; y++) {
                memcpy(dst_uv + y * g_width,
                       src_uv + y * frame.stride,
                       g_width);
            }

            written = write(g_v4l2_fd, repacked.data(), expected_size);

            if (written < 0) {
                if (errno == EAGAIN || errno == EINTR)
                    continue;
                /* ENODATA is normal when no reader attached */
                if (errno == ENODATA)
                    continue;
                fprintf(stderr, "Warning: v4l2 write failed: %s\n",
                        strerror(errno));
            }
        }
    }

    printf("V4L2 writer thread stopped\n");
    return nullptr;
}

/* ------------------------------------------------------------------ */
/* Usage                                                               */
/* ------------------------------------------------------------------ */

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [options]\n"
        "\n"
        "Options:\n"
        "  -c <id>     Camera ID (default: 0)\n"
        "  -d <dev>    V4L2 loopback device (default: /dev/video10)\n"
        "  -w <width>  Width (default: 1280)\n"
        "  -h <height> Height (default: 720)\n"
        "  -f <fps>    Framerate (default: 30)\n"
        "  -?          Show this help\n"
        "\n"
        "Camera is activated only when an application opens the device.\n"
        "\n"
        "Example:\n"
        "  modprobe v4l2loopback video_nr=10 exclusive_caps=1\n"
        "  %s -c 0 -d /dev/video10 -w 1920 -h 1080\n",
        prog, prog);
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    int opt;

    while ((opt = getopt(argc, argv, "c:d:w:h:f:?")) != -1) {
        switch (opt) {
        case 'c':
            g_camera_id = atoi(optarg);
            break;
        case 'd':
            g_device_path = optarg;
            break;
        case 'w':
            g_width = atoi(optarg);
            break;
        case 'h':
            g_height = atoi(optarg);
            break;
        case 'f':
            g_fps = atof(optarg);
            break;
        case '?':
        default:
            usage(argv[0]);
            return (opt == '?') ? 0 : 1;
        }
    }

    printf("Camx V4L2 Bridge (on-demand)\n");
    printf("  Camera:  %d\n", g_camera_id);
    printf("  Device:  %s\n", g_device_path);
    printf("  Size:    %dx%d\n", g_width, g_height);
    printf("  FPS:     %.0f\n", g_fps);

    /* Setup signal handlers */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* Setup v4l2loopback */
    g_v4l2_fd = setup_v4l2loopback(g_device_path, g_width, g_height);
    if (g_v4l2_fd < 0) {
        fprintf(stderr, "Error: Is v4l2loopback module loaded?\n");
        fprintf(stderr, "  modprobe v4l2loopback video_nr=10\n");
        return 1;
    }

    /* Connect to cam-server */
    if (connect_cam_server() < 0) {
        close(g_v4l2_fd);
        return 1;
    }

    /* Probe if camera hardware exists */
    int probe_ret = probe_camera();
    if (probe_ret != 0) {
        fprintf(stderr, "Error: Camera %d hardware not present, exiting.\n",
                g_camera_id);
        disconnect_cam_server();
        close(g_v4l2_fd);
        return probe_ret;  /* Exit code 2 = no camera, don't restart */
    }

    /* Write a black frame to make v4l2loopback advertise capture caps */
    write_black_frame();
    printf("Initial frame written, device caps exposed\n");

    /* Start device monitor thread */
    pthread_t monitor_thread;
    pthread_create(&monitor_thread, nullptr, device_monitor_thread, nullptr);

    /* Start V4L2 writer thread */
    pthread_t writer_thread;
    pthread_create(&writer_thread, nullptr, v4l2_writer_thread, nullptr);

    printf("Bridge ready. Camera will activate when %s is opened.\n",
           g_device_path);
    printf("Press Ctrl+C to stop.\n");

    /* Main loop - wait for signal */
    while (g_running) {
        sleep(1);
    }

    printf("\nShutting down...\n");

    /* Cleanup - skip QMMF IPC on signal or abnormal exit to avoid blocking */
    if (g_signal_shutdown || g_abnormal_exit) {
        printf("Skipping QMMF cleanup (signal or server died)\n");
    } else {
        deactivate_camera();
        disconnect_cam_server();
    }

    /* Wake up threads */
    g_running = false;
    g_frame_cv.notify_all();

    /* Don't block on join during shutdown - process is exiting anyway */
    if (!g_signal_shutdown) {
        pthread_join(monitor_thread, nullptr);
        pthread_join(writer_thread, nullptr);
    } else {
        pthread_detach(monitor_thread);
        pthread_detach(writer_thread);
    }

    if (g_v4l2_fd >= 0) {
        close(g_v4l2_fd);
        g_v4l2_fd = -1;
    }

    printf("Bridge stopped.\n");
    return g_abnormal_exit ? 1 : 0;
}
