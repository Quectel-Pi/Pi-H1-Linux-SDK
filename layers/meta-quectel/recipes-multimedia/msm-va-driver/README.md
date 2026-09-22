# msm-va-driver — VA-API for the QCS6490 video codec

Lets ordinary VA-API applications (mpv/VLC/GStreamer/ffmpeg/Firefox) reach the
hardware H.264/HEVC/VP9 decoder exposed by Qualcomm's `iris_vpu.ko`
(`msm_vidc`, `/dev/video32`) instead of decoding on the CPU.

```
layers/meta-quectel/recipes-multimedia/msm-va-driver/
├── build.sh                        # build (and optionally install) on the board
├── msm-va-driver_1.0.bb            # Yocto recipe for the Yocto image
├── qcm6490-msm_vidc-adapt.patch    # the fork's delta vs upstream, for review
├── verify.sh                       # board-side checks, see Testing
└── files/                          # the sources the recipe builds, kept in sync
```

Base project: <https://github.com/snowf14k3/venus-vaapi-driver> (MIT), a VA-API
backend for Qualcomm's *stateful V4L2 M2M* codec. Its `msm_drv_video.so`
implements the VA driver, an H.264 Annex-B assembler (VA gives parsed
SPS/PPS/slice structures, the kernel driver wants an elementary stream), the
V4L2 M2M session and the NV12 download path.

## Install

The driver is built by the `msm-va-driver` recipe from the GitHub fork, so
nothing is checked in as a binary:

```
SRC_URI = git://github.com/super617/venus-vaapi-driver.git \
          ;protocol=https;branch=qcm6490-msm-vidc-adapt
SRCREV  = 095cf6aa4cc73bfe68f41b17ef39c7b8182d5fdc   # pinned on purpose
```

`quecpi-image.bb` lists the package in `IMAGE_INSTALL`, and the recipe is the
only source of the file — change the fork, bump `SRCREV`, rebuild.

`do_install` writes the same binary to two directories, because this SDK ships
two rootfs flavours:

| path | used by |
|------|---------|
| `${libdir}/dri/msm_drv_video.so` | Yocto rootfs (libva searches `${libdir}/dri`) |
| `/usr/lib/aarch64-linux-gnu/dri/msm_drv_video.so` | the Debian/Ubuntu rootfs the SDK also builds |

The Debian/Ubuntu image needs the multiarch copy because of *when* the rootfs is
assembled: `deploy_debian_gnome_rootfs()` runs from `IMAGE_PREPROCESS_COMMAND`,
i.e. in `do_image`, **after** `do_rootfs` has installed the packages. So
`msm-va-driver` is installed first and the Debian/Ubuntu rootfs overlay is then
rsynced on top of it — without `--delete`, and with no `msm_drv_video.so` of its
own (checked in both `debian-gnome-rootfs` and the `ubuntu26` overlay), so the
recipe's file is the one that ships. The two directories come from one build
step, so they cannot drift.

On a running board (gcc and libva-dev are enough):

```sh
apt-get install -y libva-dev
./build.sh --install
vainfo
```

`build.sh` compiles `files/` in place and is what `verify.sh` and board-side
bring-up use; it is not part of the image build.

libva resolves the DRM driver name `msm` to `msm_drv_video.so` on its own, so
no `LIBVA_DRIVER_NAME` is needed — `vainfo` and every VA-API application pick
the driver up as soon as the file exists. Remove the file to uninstall.

## What our adaptation changes

`files/` is the source of truth for this recipe; `qcm6490-msm_vidc-adapt.patch`
is the same delta rendered as a patch against upstream `47744788`, regenerated
with `git diff 47744788..HEAD` in the fork, so a reviewer can read the whole
adaptation in one file. The recipe does not apply it — it builds the branch.

The fixes it contains, i.e. what makes this board work at all:

1. **Accept the downstream driver name.** Upstream only claimed devices whose
   V4L2 `driver` is `qcom-venus`; `iris_vpu.ko` reports `msm_vidc_driver`.
   One shared predicate now decides this for probe, decoder and encoder.
2. **Surface/frame matching context.** The frame callback needs the owning VA
   context to count pictures in and frames out, so the pump carries the context
   instead of the backend.
3. **Drain on stall.** A stateful decoder emits a reordered frame only after
   the *next* picture arrives. A client whose own surface pool is exhausted
   cannot submit that picture and deadlocks against the decoder; a stalled
   `vaSyncSurface` therefore drains the decoder (`V4L2_DEC_CMD_STOP`, then
   `V4L2_DEC_CMD_START` to resume). Without it the sync times out after 30 s and
   players drop back to software decoding.
4. **Stop/start pair.** `VENUS_*_stop()` existed but nothing resumed the
   session, which left it unusable for further pictures.
5. **`vaExportSurfaceHandle` (DMA-BUF export).** Surfaces are now allocated from
   a DMA-BUF heap (`/dev/dma_heap/qcom,system`, falling back to
   `/dev/dma_heap/system`) instead of plain `calloc`, and the driver exports
   them as linear NV12 fds. This is what unlocks zero-copy: mpv resolves
   `vaExportSurfaceHandle` and refuses hardware decoding without it. The heap
   memory stays CPU-mappable, so the copy-based download path is unaffected;
   without an accessible heap the driver degrades to plain memory and reports
   export as unsupported instead of failing.
6. **Small surfaces are memory, not pictures.** The decoder's minimum picture
   size (48x32) was also being applied to surfaces, which made mpv's 16x16
   hwupload probe fail with `VA_STATUS_ERROR_INVALID_PARAMETER`. Surfaces now
   have their own floor (16x16, even dimensions required by the NV12 layout).
7. **`vaAcquireBufferHandle` / `vaReleaseBufferHandle`.** These were stubs
   returning `VA_STATUS_ERROR_INVALID_BUFFER`. VLC's GL interop requires them:
   before it displays anything it probes the surface pool by deriving an image
   and acquiring a buffer handle for it, and gives up on the whole pool when
   that fails. The handle handed out is a `dup()` of the surface's DMA-BUF fd,
   so the client can close it whenever, and it is dropped when the buffer or the
   image behind it is destroyed or the driver terminates.
8. **The export layer layout follows the flags.** `vaExportSurfaceHandle` used to
   answer with one composed NV12 layer (two planes) no matter what the caller
   asked for. VLC passes no layout flag and then refuses any layer holding more
   than one plane, so it dropped *every* frame into a texture that was never
   filled — a flat green rectangle where the video should be. The driver now
   returns one layer per plane unless `VA_EXPORT_SURFACE_COMPOSED_LAYERS` is
   explicitly requested, which is also the layout `VA_EXPORT_SURFACE_SEPARATE_LAYERS`
   documents for NV12. mpv/ffmpeg are unaffected (verified on screen).

## Status (measured on the board, kernel 6.6.116-qli-1.7-ver.1.1)

Works:

- `vainfo` loads the driver and reports H.264 Baseline/Main/High VLD and
  EncSlice, HEVC Main and VP9 decode;
- H.264 decode is **bit-exact against software decoding** — 640x480 baseline
  and 1280x720 High agreed byte for byte, including the SHA-256 the upstream
  project documents for its own board
  (`5a5aa547019fe1c8e33bf682a619c24562efc370ceee31b3d93be2fa22c70cb8`);
- `ffmpeg -hwaccel vaapi -vaapi_device /dev/dri/renderD128` decodes to the end
  of a stream and downloads NV12 through `hwdownload`;
- **mpv plays with hardware decoding, zero-copy.** `mpv --hwdec=vaapi` (direct,
  auto-inserted by default) reports `Using hardware decoding (vaapi)` and
  `[vo/gpu/vaapi] Using EGL dmabuf interop via GL_EXT_EGL_image_storage`, i.e.
  the decoded surface is imported as an EGLImage and sampled on the GPU. Verified
  on screen: the video area was 1280x720 at the correct position with the right
  colours in every quadrant, and matched the software render to within 1 LSB
  (see Testing).

- **VLC plays with hardware decoding, zero-copy.** `vlc --avcodec-hw=vaapi`
  decodes 4K H.264 on the VPU and displays it from the exported DMA-BUF through
  `glconv_vaapi_wl` (`glconv_vaapi_x11` under XWayland, same code). Measured on
  the board with a 3840x2160 29.97 fps clip: 30 fps sustained for the whole run,
  no dropped frames, ~16% of all cores busy, `VENUS_VAAPI_LOG=1` showing ~60
  exports/s and no GL errors. On screen the video area shows the clip on every
  patch checked; `verify.sh` now asserts exactly that, because the failure mode
  before fixes 7 and 8 was a flat green picture with the progress bar running.

  VLC needs a window, so it runs under the session's compositor: on this Debian
  image its Qt interface falls back to xcb/XWayland, which needs no extra
  packages. `qtwayland5` makes it Wayland-native instead; both paths were
  measured and both display correctly.

Not reliable yet:

- **H.264 with B-frames / reordering.** `./verify.sh` runs 10 trials each time
  and the bit-exact pass rate against software decode lands anywhere from 3/10
  to 9/10 for the same 150-frame 1280x720 `-bf 3` clip — 52/80 measured across
  the runs made while writing this, i.e. roughly two runs in three, and there is
  no way to tell from the outside which you will get. The failing runs are the
  client/decoder deadlock described below: the drain recovers the stream but the
  flushed references break, so they either ghost for ~1 s or return
  `VA_STATUS_ERROR_DECODING_ERROR` (23) and fall back to software.
  `mpv --hwdec=vaapi` therefore falls back on typical B-frame content.

- **4K decodes ~1.3x realtime, so there is not much headroom.** `ffmpeg
  -hwaccel vaapi -f null -` measures 38 fps on a 3840x2160 29.97 fps clip
  (62 fps on synthetic 4K, 74 fps at 1080p), and 1.3x is thin enough that a 4K
  player occasionally reports `picture is too late to be displayed` by tens of
  milliseconds even with no dropped frames. The obvious cause is that every
  decoded frame is CPU-copied out of the V4L2 capture buffer into the surface's
  DMA-BUF (`venus_decode_store_frame_locked`), which is 12.4 MB per frame at 4K.
  Importing the decoder's buffers into V4L2 instead
  (`V4L2_MEMORY_DMABUF`) would remove that copy; not attempted.

- **A benign Mesa warning.** While a VA-API surface is imported, Mesa's msm
  driver logs `Failed to set BO metadata with DRM_MSM_GEM_INFO: -22` — the
  kernel declines a caching hint for a buffer Mesa did not allocate. It does not
  affect the result: the displayed picture was pixel-correct (see Testing).

## 4K playback, and proving the VPU is the one decoding

`DJI_0010.MP4` — 3840x2160 H.264 High, 100 Mbps, 617 frames, 21 I + 596 P (no
B-frames), 20.6 s — is the kind of clip the hardware path exists for. Measured
on the board (`ffmpeg`, whole file, 8 cores):

| path | wall | CPU time | decode rate |
|------|------|----------|-------------|
| `ffmpeg -hwaccel vaapi ... -f null -` | 15.0 s | 12.2 s | 41 fps (1.37x realtime) |
| `ffmpeg` (software) | 16.3 s | 108.9 s | 38 fps (1.27x realtime) |

Same wall time, ~9x less CPU. `mpv --hwdec=vaapi-copy` plays the clip to EOS
(`[vd] Using hardware decoding (vaapi-copy)`, `VO: [gpu] 3840x2160 nv12`).

The CPU load that remains is ~1 core, and it is not the decode: the driver
still copies every frame out of the V4L2 buffer into a VA surface
(`files/src/decode.c:112-123`, row by row, 12.4 MB per 4K NV12 frame). That copy
is inside the driver and is what a future V4L2-buffer-import or decoder-DMABUF
path would remove. What `vaExportSurfaceHandle` fixed is the *next* step: the
surface is now a DMA-BUF the GPU can sample directly, instead of a CPU buffer
the player had to upload again.

That the decode really is on the VPU — rather than silently in software — is
checkable while a decode runs:

```sh
pid=$(pgrep -f 'ffmpeg.*DJI' | head -1)
ls -l /proc/$pid/fd | grep video       # -> /dev/video32
cat /sys/kernel/debug/msm_vidc/core/inst_*/info
#   INSTANCE ... (Decoder) / width: 3840 / height: 2176
#   ETB/EBD/FTB/FBD counters climb as frames come out
fuser -v /dev/video32
```

Those counters are cumulative — sample twice and subtract. Over a 6 s window on
this clip the VPU delivered 242 frames (40.2 fps) while ffmpeg reported 39.4
fps, i.e. app and VPU run in lock step and the application, not the VPU, sets
the pace. Use `cat` on those debugfs files; `head` fails with "cannot seek to
relative offset".

### Where the time actually goes

Whole 617-frame clip, each configuration repeated three times (stable to ±1 fps):

| configuration | 4K decode rate | CPU time |
|---------------|----------------|----------|
| `ffmpeg -hwaccel vaapi ...` (default: one hwaccel thread per core) | 40-41 fps | 12.9 s |
| `ffmpeg -threads 1 -hwaccel vaapi ...` | 71 fps | 6.4 s |
| 1080p, default | 70 fps | 8.5 s |
| 1080p, `-threads 1` | 335 fps | 2.2 s |

`-threads 1` is therefore worth 1.75x at 4K and 4.8x at 1080p. With several
hwaccel threads, several VA contexts download decoded frames at once, and that
download is a plain CPU read of the V4L2 capture buffer. That read runs at
1.32 GB/s where the same copy in ordinary memory runs at 10.8 GB/s — the capture
buffer is mapped uncached — so concurrent readers halve each other: the driver's
copy costs 9.4 ms per 4K frame with `-threads 1` and 20.9 ms at the default
thread count (measured with `COPYSTAT` counters in an instrumented build; the
read is the whole cost, replacing the destination write with an L1-resident
scratch does not change the number).

With `-threads 1` the copy no longer sets the pace, it hides behind the VPU:

| copy in the driver | 4K | 1080p |
|--------------------|-----|-------|
| as shipped | 71 fps | 335 fps |
| read only, no destination write | 71 fps | — |
| skipped entirely | 71 fps | 357 fps |

71 fps at 4K is the VPU's own ceiling (QCS6490 is specified for 4K60). Removing
the copy for good — a decoder path that hands the V4L2 buffer out instead of
copying it into a VA surface — is worth ~9.4 ms of CPU per 4K frame and fixes
the slow default-threaded case, but it buys no fps at 4K.

What drops frames on this clip is presentation, not decode: the decoder reports
zero dropped frames, the video output drops them. On a 4K30 clip, 300 frames
(10.0 s of content), alternating runs so drift cancels out:

| video output | dropped | wall |
|--------------|---------|------|
| `vo=dmabuf-wayland` + copy | 0, 1, 11 | 10.5 s |
| `vo=dmabuf-wayland` + direct vaapi | 32, 37, 35, 8 | 10.5 s |
| `vo=gpu` (GL) + direct vaapi | 95, 94, 98, 81 | 11.1 s |
| `vo=gpu` (GL) + copy | 85, 89, 73 | 11.1 s |

The GPU clock is not the limit (the ondemand governor already reaches the
550 MHz maximum). The GL path drops frames while scaling 4K to the 1080p panel;
the Wayland dmabuf passthrough does not.

The SDK therefore ships `prebuild/gnome/etc/mpv/mpv.conf` (the `gnome` overlay
entry sits after `bsp-fix` in `prebuild/sync-list`, so it is the last word on
desktop configuration):

```ini
hwdec=vaapi-copy

[large-frames]
profile-desc=DMABUF passthrough for >=1440p sources
profile-cond=width ~= nil and height ~= nil and (width >= 2560 or height >= 1440)
profile-restore=copy
vo=dmabuf-wayland,gpu
```

With that file and no command-line options: 4K30 drops 0-2 frames, 4K30 with
B-frames 1-12 and no longer stalls, and 1080p stays on `vo=gpu` with 0-1 drops.
Two traps are worth repeating: mpv's default is `hwdec=no`, and the nil guards
in `profile-cond` are mandatory — `width`/`height` are unset until the stream is
known, and comparing nil raises a Lua error that aborts playback with
`Errors when loading file`. Small frames deliberately keep `vo=gpu`, because
`dmabuf-wayland` renders video only and would cost OSD and subtitles.

## Where the reordering problem really is

`msm_vidc` outputs capture frames in **display order**
(`HFI_PROP_DECODE_ORDER_OUTPUT` is left at the platform default, see
`platform/qcm6490/src/msm_vidc_qcm6490.c` in the vendor `video-driver` tree:
`{OUTPUT_ORDER, DEC, H264|HEVC|VP9, 0, 1, 1, 0, ...}`), so frames that a client
has not asked for yet sit in the decoder until the next picture arrives.

VA-API does not need that: the client already submits pictures in decode order
and reorders for display itself (libavcodec), and this driver matches decoded
frames to surfaces by tag. If the decoder emitted each frame as soon as it was
decoded, no frame would ever be held, `vaSyncSurface` would always be
satisfiable, and the deadlock — and the drain workaround with it — would
disappear. That is a change in the vendor kernel driver or its platform data,
not in this library.

## Testing

`./verify.sh` does all of the below on a board that `adb` can reach, building
from `files/`. Run it after touching anything in `files/`. It checks that:

0. no msm_vidc session is still held by a player from an earlier run. A stale
   session makes checks 3 and 4 fail the same way a real driver regression does,
   so the script kills leftover players and resets the codec first instead of
   reporting a driver bug that is not there. If it prints
   `reset msm_vidc, cleared N stale session(s)`, that is the guard working;
1. `files/` still compiles into a loadable driver (built on the board);
2. libva picks the driver up with no environment variables set;
3. H.264 decode is bit-exact against software decode, and reports the B-frame
   pass rate (the known limitation, so a regression there is visible);
4. `vaExportSurfaceHandle` returns a DMA-BUF the application can `mmap`
   (`files/tests/va_export_test.c` — quadrant-free, the API-contract half);
5. the picture actually reaches the screen: mpv plays a 2x2 solid-colour clip
   once with `--hwdec=vaapi` and once without, both are screenshotted, and
   `files/tests/screenshot_compare.py` verifies the colours and geometry of the
   hardware render and that it matches the software one to within 1 LSB
   (`files/tests/zero_copy_display.sh`). This is the end-to-end proof that the
   exported DMA-BUF is really what the GPU samples.

   Getting that screenshot reliably took three fixes, because the first version
   reported capture races as decode mismatches:

   - the player runs full screen (`--fs`); windowed, it could end up behind
     another window and then no amount of waiting produced anything but the
     desktop;
   - the capture is retried until the frame really shows the clip. The check
     samples two patches with `ffmpeg` (`crop`, `scale=1:1`) rather than
     assuming a sleep was long enough — mpv logging its video output does not
     mean the compositor has put it up yet, and the board has no PIL;
   - the player is killed by a unique `--title`, not by process name: an
     unrelated mpv left running on the board would otherwise make the wait loop
     spin forever, and a surviving player holds both the Wayland surface and a
     VPU session;
   - a miss retries the *player*, not just the screenshot. When the surface
     never got mapped, re-shooting the desktop five times can never recover, so
     each phase relaunches (up to three times) before giving up. Seen once in
     the runs made while writing this, on the control playback; never on the
     hardware one.

   How that last case is reported matters. `zero_copy_display.sh` exits 1 when
   the hardware picture is missing or wrong — a flat green window has no test
   pattern either, so that stays a failure — and 3 when the hardware picture was
   fine but the software control never reached the screen. Software decode
   cannot be broken by this driver, so `verify.sh` prints a `SKIP` for exit 3
   instead of failing. Before that split, a compositor hiccup over the control
   clip arrived looking exactly like a driver regression.

   `screenshot_compare.py` also distinguishes the two failure kinds now: a
   screenshot without the test pattern is reported as a capture problem, while
   wrong pixels in a present pattern are reported as a picture mismatch.
   Its banding check only looks at differences above 1 LSB — rounding noise is
   scattered and harmless, and counting it made the check fire on good
   full-screen captures.

   With those in place the check is stable: nine consecutive runs (six capture
   runs plus three full `verify.sh` passes) all produced byte-identical
   screenshots and 3518/2073600 differing pixels at a maximum of 1 LSB. One
   earlier run, still windowed, did show 92381 differing pixels at a maximum of
   255 with the pattern correctly placed; it has not recurred since the player
   went full screen and is recorded here in case it turns up again.

6. VLC does the same on screen, because it reaches the driver through different
   entry points than mpv: it probes its surface pool with `vaDeriveImage` +
   `vaAcquireBufferHandle` and then imports every frame from
   `vaExportSurfaceHandle` (`files/tests/vlc_display.sh`, same 2x2 clip and the
   same `ffmpeg` patch sampling). This is the check that catches the green
   screen: with the composed layer layout it fails, with separate layers it
   passes, and both directions were run. VLC needs an interface that can give
   its video output a window, so the script retries with `QT_QPA_PLATFORM=xcb`
   after the session default, waits for a previous instance to exit first, and
   exits with 2 — which `verify.sh` reports as a skip — when no window can be
   had at all, since that says nothing about the driver.

```sh
./verify.sh                 # uses ../../../../quectel_build/tools/adb
./verify.sh /path/to/adb    # or a specific adb
```

Keep the raw dumps off `/tmp`: it is a 3.6 GB tmpfs on this board and a full
`/tmp` truncates the NV12 files, which shows up as phantom decode mismatches.
`verify.sh` uses `/var/tmp` for that reason (and cleans up after itself).

Checks 5 and 6 need a graphical session and `gnome-screenshot`
(`apt-get install -y gnome-screenshot`) plus `python3` with `numpy`/`Pillow` on
the host; check 6 also needs `vlc`. They are skipped with a note, not failed,
when those are missing.

Measuring playback by hand (VLC, 4K):

```sh
# decode throughput, no display involved
ffmpeg -hwaccel vaapi -hwaccel_output_format vaapi -i clip.mp4 -f null -

# what the driver is asked to do while a player runs
VENUS_VAAPI_LOG=1 vlc --avcodec-hw=vaapi clip.mp4
grep -c 'export-surface' vlc.log        # frames handed to the GL interop
grep -a 'picture is too late' vlc.log   # display-side lateness
```

By hand:

```sh
vainfo

ffmpeg -f lavfi -i testsrc2=size=1280x720:rate=30 -frames:v 90 \
       -c:v libx264 -profile:v high -bf 0 -g 30 -pix_fmt yuv420p in.h264
ffmpeg -i in.h264 -pix_fmt nv12 -f rawvideo sw.nv12
ffmpeg -hwaccel vaapi -hwaccel_device /dev/dri/renderD128 \
       -hwaccel_output_format vaapi -f h264 -i in.h264 \
       -vf hwdownload,format=nv12 -pix_fmt nv12 -f rawvideo hw.nv12
cmp sw.nv12 hw.nv12

# V4L2 M2M path without VA-API (reference for bring-up debugging)
#   gcc tools/venus-v4l2-decode.c ... from the upstream project
```
