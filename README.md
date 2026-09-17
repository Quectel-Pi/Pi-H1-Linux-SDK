Quectel PI H1 build
=====

QSM565DWF (QCM6490 / QCS6490) Linux SDK. Builds Yocto images for the standard
Linux firmware, and Debian / Ubuntu images on top of a prebuilt GNOME rootfs.

## Host Requirements

| Component           | Recommended Configuration                           |
| ------------------ | -------------------------------------------------- |
| CPU                | **Intel i7-8700**, or **AMD Ryzen 5 3600 / 5600X** |
| Disk Space         | **300 GB** free (Swap > **32 GB**)                 |
| RAM                | **16 GB** (recommended **32 GB**)                  |
| Operating System   | **Ubuntu 22.04**                                   |

## Get SDK

    git clone https://github.com/Quectel-Pi/Pi-H1-Linux-SDK.git

## Environment Setup

    cd your/path/to/Pi-H1-Linux-SDK/

    ./install_build_env.sh

## Build

Every build starts by sourcing the build environment, then picking a system and
a version with `buildconfig`:

    cd your/path/to/Pi-H1-Linux-SDK/

    source quectel_build/compile/build.sh

    buildconfig QSM565DWF <Your Project ID> LINUX STD

    buildall            # complete build (cleanall + bitbake, slow)

    buildpackage        # pack the firmware into quectel_build/<Your Project ID>

`buildconfig <project> <project ID> <OS> <VERSION> [SEC]` - two dimensions,
each one takes a single value:

| OS       | Firmware                                                          |
| -------- | ----------------------------------------------------------------- |
| `LINUX`  | Standard Linux firmware (Yocto)                                    |
| `UBUNTU` | Ubuntu firmware, prebuilt GNOME rootfs is downloaded if missing    |
| `DEBIAN` | Debian firmware, prebuilt GNOME rootfs is downloaded if missing    |

| VERSION | Firmware                                                          |
| ------- | ----------------------------------------------------------------- |
| `STD`   | Standard build, performance (default)                              |
| `DBG`   | Debug build, debug symbols kept (`INHIBIT_PACKAGE_STRIP`) and dump enabled |

`SEC` is the only optional flag: secure boot build (`SECBOOT_ENABLE=1`). It can
be added to any `OS`/`VERSION` combination.

The project ID can be any value you like; it is what names the firmware
directory, e.g. `quectel_build/<Your Project ID>`. A dimension that is not the
default (`LINUX`, `STD`) is appended to the firmware directory name, so the two
versions of one system never overwrite each other:

| Invocation   | Firmware directory                                        |
| ------------ | --------------------------------------------------------- |
| `LINUX STD`  | `quectel_build/<Your Project ID>`                          |
| `LINUX DBG`  | `quectel_build/<Your Project ID>_DBG`                      |
| `DEBIAN DBG` | `quectel_build/<Your Project ID>_DEBIAN_DBG`               |

After the first `buildall`, day to day work should build incrementally:

    source quectel_build/compile/build.sh
    buildconfig QSM565DWF <Your Project ID> LINUX STD
    bitbake $TARGET_IMAGE
    buildpackage

## Build Debian firmware

    cd your/path/to/Pi-H1-Linux-SDK/

    source quectel_build/compile/build.sh

    buildconfig QSM565DWF <Your Project ID> DEBIAN STD

    buildall

    buildpackage

`buildconfig ... DEBIAN ...` selects the Debian customisation, copies
`quectel_build/compile/quectel-features-config/debian-sync-list` to
`prebuild/sync-list` (the files synced into the rootfs) and downloads
`debian-gnome-rootfs.tar.xz` (~1.1 GB) from the
[pi-rootfs](https://github.com/super617/pi-rootfs/releases) release if it is not
already present in `prebuild/`. The download is resumable: an interrupted run
keeps a `.part` file and continues with `-C -` on the next `buildconfig`. Once
the tarball is in place, `buildall` and `buildpackage` are the same as above.

## Build Ubuntu firmware

    cd your/path/to/Pi-H1-Linux-SDK/

    source quectel_build/compile/build.sh

    buildconfig QSM565DWF <Your Project ID> UBUNTU STD

    buildall

    buildpackage

Same flow as Debian, with `ubuntu-sync-list` and `ubuntu26-gnome-rootfs.tar.xz`
(~1.2 GB). `LINUX` builds do not need a prebuilt rootfs: they set
`SKIP_DEPLOY_DEBIAN_GNOME_ROOTFS = "1"`.

## Debug and secure boot builds

    # debug build (dump enabled, debug symbols kept)
    buildconfig QSM565DWF <Your Project ID> LINUX DBG

    # standard firmware, secure boot
    buildconfig QSM565DWF <Your Project ID> LINUX STD SEC

    # debug build with secure boot
    buildconfig QSM565DWF <Your Project ID> LINUX DBG SEC

SEC boot output needs the signing package, so finish with `buildpackage`.
The firmware directory name carries the non-default dimensions, so two variants
of one project ID do not collide; using a different project ID per variant is
still the clearest way to tell the packages apart.

## Get your firmware

    quectel_build/<Your Project ID>

## Flash

    ./quectel_build/tools/flash.sh          # UFS, default
    ./quectel_build/tools/flash.sh emmc     # eMMC

The script reads the firmware directory from
`quectel_build/compile/quectel-features-config/quectel-buildconfig-gen.h`, so run
`buildconfig` and `buildpackage` first. It then checks the ADB device, reboots
into EDL (9008), and flashes with `qdl`, keeping the `persist` partition.

On QCM6490 use `adb shell reboot edl` to enter EDL - `adb reboot edl` does not
work on this board. If flashing fails with `qdl: failed to claim USB interface`,
power-cycle the board and try again.

## Build kernel & dtb for Debian

    cd your/path/to/Pi-H1-Linux-SDK/

    source quectel_build/compile/build.sh

    buildconfig QSM565DWF <Your Project ID> DEBIAN STD

    buildkernel                 (build kernel into efi.bin)

    builddtb                    (build dtb into dtb.bin)

## Get your kernel && dtb and replace into debian package

    quectel_build/output/efi.bin

    quectel_build/output/dtb.bin

Both are regenerated by `buildkernel` / `builddtb`; the directories they live in
are build scratch space and are not tracked.

## Other commands

| Command             | Purpose                                                        |
| ------------------- | -------------------------------------------------------------- |
| `buildall`          | `bitbake $TARGET_IMAGE -c cleanall` followed by a full build     |
| `buildconfig`       | Select project / project ID / OS / version (writes the config)   |
| `buildpackage`      | Collect image, boot binaries, firehose and partition files       |
| `buildkernel`       | Rebuild the kernel and drop it into `quectel_build/output/efi.bin` |
| `builddtb`          | Rebuild the dtb and drop it into `quectel_build/output/dtb.bin`  |
| `buildsdk`          | Build the image and export the SDK                               |
| `buildesdk`         | Build the image and export the extensible SDK                    |
| `do_kernel_images`  | Re-run the kernel/EFI packaging step only                        |
| `buildenv`          | Print the currently selected project / project ID / OS / version |
| `enter_rootfs`      | Enter the Debian rootfs shell                                    |
| `rebake <recipe>`   | `bitbake <recipe> -c cleansstate` followed by a rebuild          |
| `flash [ufs|emmc]`  | Flash the packed firmware                                        |

## Incremental development

    source quectel_build/compile/build.sh
    buildconfig QSM565DWF <Your Project ID> LINUX STD

    # rebuild a single package
    bitbake <recipe> -c compile -f

    # rebuild a single kernel module / kernel change
    bitbake <recipe> -c cleansstate && bitbake <recipe>

Applications can be tested without flashing:

    adb root && adb remount
    adb push <binary> <path on device>
    adb shell sync && adb shell reboot

Never kill a running bitbake with `kill -9` / `pkill`: it corrupts the
`sstate-cache` and the build environment. Use `Ctrl+C` if you really need to
stop it.
