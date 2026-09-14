require qcom-console-image.bb

SUMMARY = "Basic Wayland image with Weston"

LICENSE = "BSD-3-Clause-Clear"

# let's make sure we have a good image.
REQUIRED_DISTRO_FEATURES += "wayland"

CORE_IMAGE_BASE_INSTALL += " \
    packagegroup-qcom-multimedia \
"

#Provide log header support on SDK
TOOLCHAIN_TARGET_TASK:append = " syslog-plumber-dev protobuf-c-dev"

#Provide camera header support on SDK
TOOLCHAIN_TARGET_TASK:append:qcom-custom-bsp:qcm6490 = " camxapi-kt-dev"
TOOLCHAIN_TARGET_TASK:append:qcom-custom-bsp:qcs9100 = " camxapi-dev"
TOOLCHAIN_TARGET_TASK:append:qcom-custom-bsp:qcs8300 = " camxapi-dev"

IMAGE_OVERHEAD_FACTOR = "1.2"
EXTRA_USERS_PARAMS:append = " usermod -p '' root;"
IMAGE_FEATURES:remove = "read-only-rootfs"
IMAGE_INSTALL:append = " yt6801 r8168 qca1023-wlan"
IMAGE_INSTALL:append = " quec-pcie-mhi quec-sprd-pcie"
IMAGE_INSTALL:append = " quectel-cm quectel-firehose quectel-log"
IMAGE_INSTALL:append = " quectel-tests lgpio libcamera"
IMAGE_INSTALL:append = " atci key-event-handler quecpi-config"
IMAGE_INSTALL:append = " quectel-hdmi-firmware"
IMAGE_INSTALL:append = " kernel-devsrc"
IMAGE_INSTALL:append = " quectel-ota"
IMAGE_INSTALL:append = " quec-ufs-size"
IMAGE_INSTALL:append = " v4l2loopback camx-v4l2-bridge"   

IMAGE_INSTALL:append = " \
                    e2fsprogs \
                    dtc \
                    sysdig-quectel \
                    btrfs-tools \
                    qcom-npu-test \
                "

IMAGE_INSTALL:remove = " \
                    kernel-module-qcacld-wlan \
                "

IMAGE_INSTALL:remove = "qcom-resize-partitions wlan-sigma-dut"

# Writes debian rootfs

DEPENDS += "rsync-native"

deploy_debian_gnome_rootfs () {

        local list_file="${TOPDIR}/../prebuild/sync-list"
        local dirs

        local rsync_excludes="
        --exclude=/dev
        --exclude=/sys
        --exclude=/proc
        --exclude=/mnt
        --exclude=/root
        --exclude=/boot
        --exclude=/var/cache
        --exclude=/run
        --exclude=/usr/lib/udev/hwdb.bin
        --exclude=/var/log
        --exclude=/var/lib/apt/lists/lock
        --exclude=/var/lib/dpkg/lock
        --exclude=/var/lib/dpkg/lock-frontend
        --exclude=/var/lib/dpkg/triggers/Lock
        --exclude=/etc/.pwd.lock
        --exclude=/etc/ssl
        --exclude=/var/spool/rsyslog
        --exclude=/var/crash
        --exclude=/var/lib/apt/lists/partial
        --exclude=/home/pi/.cache
        --exclude=/usr/lib/python3.13/EXTERNALLY-MANAGED
        --exclude=/etc/ssh/ssh_host_rsa_key
        --exclude=/etc/ssh/ssh_host_ecdsa_key
        --exclude=/etc/ssh/ssh_host_ed25519_key
        --exclude=/etc/ssh/ssh_host_rsa_key.pub
        --exclude=/etc/ssh/ssh_host_ecdsa_key.pub
        --exclude=/etc/ssh/ssh_host_ed25519_key.pub
        --exclude=/etc/ppp
        --exclude=/usr/share/chrony/chrony.keys
        "

        dirs="$(cat "$list_file")"

        for d in $dirs; do
        local src_dir="${TOPDIR}/../prebuild/${d}"

        local tar_gz="${TOPDIR}/../prebuild/${d}.tar.gz"
        local tar_xz="${TOPDIR}/../prebuild/${d}.tar.xz"

        local tarball=""
        local tar_opt=""

        if [ -f "$tar_xz" ]; then
                tarball="$tar_xz"
                tar_opt="-xJf"
        elif [ -f "$tar_gz" ]; then
                tarball="$tar_gz"
                tar_opt="-xzf"
        fi

        # Decompress the prebuild tarball into src_dir when needed.
        # NOTE: src_dir is cached to avoid re-decompressing on every build.
        # But the cache can be poisoned: if a previous extraction ran under
        # sudo (or the tree was copied out of a qemu apt-upgrade with files
        # owned by real root:root), those files land as *real* root:root and
        # this build user cannot read them -> rsync fails with "Permission
        # denied (13)" / exit 23. do_image runs under pseudo, but pseudo only
        # rewrites stat()/access() return values; it does NOT bypass the
        # kernel's DAC check on open()/read().
        #
        # When tar --numeric-owner extracts under pseudo + unprivileged user,
        # files recorded as 0/0 are created with the *real* caller's ownership
        # permissions/ownership are preserved AND rsync can read the content.
        # We exploit this: re-extract whenever the cached tree contains any
        # file the current user cannot read.
        local need_extract=0
        if [ -n "$tarball" ] && [ ! -d "$src_dir" ]; then
            need_extract=1
        fi
        if [ "$need_extract" -eq 0 ] && [ -n "$tarball" ] && [ -d "$src_dir" ]; then
            # Detect files the current user cannot actually read (poisoned
            # cache from a sudo extraction or a qemu rootfs copy-out where
            # files are *real* root:root 600/640). Use -readable (an actual
            # open() readability test), NOT -perm -004: a 640 file owned by
            # *this* user is readable to us even though other has no r, and
            # -perm -004 would wrongly force a re-extract every build.
            if find "$src_dir" -type f ! -readable 2>/dev/null | read -r _bad; then
                bbnote "$src_dir: cached tree has unreadable files (poisoned by sudo/root extraction), re-extracting from $tarball"
                need_extract=1
            fi
        fi
        if [ "$need_extract" -eq 1 ]; then
                rm -rf "$src_dir"
                mkdir -p "$src_dir"
                tar --numeric-owner $tar_opt "$tarball" -C "$src_dir"
        fi

        if [ ! -d "$src_dir" ]; then
                bbfatal "prebuild source not found: $src_dir (or $tar_gz / $tar_xz)"
        fi

        rsync -aHAX --numeric-ids --inplace $rsync_excludes \
                "${src_dir}/" "${IMAGE_ROOTFS}/"
        done

        chown -R root:root ${IMAGE_ROOTFS}/
        if [ -d "${IMAGE_ROOTFS}/var/lib/gdm3" ]; then
        if grep -q '^Debian-gdm:' "${IMAGE_ROOTFS}/etc/passwd"; then
                chown -R Debian-gdm:Debian-gdm "${IMAGE_ROOTFS}/var/lib/gdm3"
        else
                bbwarn "Debian-gdm user not found, skip chown /var/lib/gdm3"
        fi
        chmod 0700 "${IMAGE_ROOTFS}/var/lib/gdm3"
        fi

        if [ -d "${IMAGE_ROOTFS}/home" ]; then
           chown root:root "${IMAGE_ROOTFS}/home"
           chmod 755 "${IMAGE_ROOTFS}/home"
        fi

        if [ -f "${IMAGE_ROOTFS}/usr/bin/sudo" ]; then
           chmod 4755 "${IMAGE_ROOTFS}/usr/bin/sudo"
        fi

        if [ -f "${IMAGE_ROOTFS}/usr/bin/pkexec" ]; then
           chmod u+s "${IMAGE_ROOTFS}/usr/bin/pkexec"
        fi

        if [ -n "${IMAGE_ROOTFS}" ] && [ -d "${IMAGE_ROOTFS}" ]; then
            rm -f ${IMAGE_ROOTFS}/usr/lib/libc.*
            rm -f ${IMAGE_ROOTFS}/usr/lib/libz.*
            rm -f ${IMAGE_ROOTFS}/usr/lib/libm.*
            rm -f ${IMAGE_ROOTFS}/usr/lib/libstdc++.*
            rm -f ${IMAGE_ROOTFS}/usr/lib/libelf*
            rm -f "${IMAGE_ROOTFS}/etc/systemd/system/multi-user.target.wants/init_display.service"
            rm -f "${IMAGE_ROOTFS}/etc/systemd/system/init_display.service"
            rm -f "${IMAGE_ROOTFS}/etc/systemd/system/multi-user.target.wants/qteesupplicant.service"
            rm -f "${IMAGE_ROOTFS}/usr/lib/systemd/system/qteesupplicant.service"
            rm -f "${IMAGE_ROOTFS}/usr/lib/systemd/system/pipewire-pulse.service"
            rm -f "${IMAGE_ROOTFS}/etc/systemd/system/multi-user.target.wants/pipewire-pulse.service"
            rm -f "${IMAGE_ROOTFS}/etc/modprobe.d/blacklist-msm.conf"

            # GPU userspace: let Debian mesa (in multiarch dir) be the single
            # provider of libEGL/libGL/libGLESv2/libGLESv1_CM/libvulkan SONAMEs.
            # QCom adreno blobs live in /usr/lib and shadow mesa via ld.so.cache
            # ordering (multiarch entry wins, but the adreno files leak into the
            # cache and break GPU/EGL on the msm_dpu kernel driver). Remove both
            # the real blobs and the SONAME symlinks pointing at them. mesa in
            # /usr/lib/aarch64-linux-gnu then becomes the sole candidate.
            # NOTE: the wildcarded `rm -f "${IMAGE_ROOTFS}/usr/lib/lib*.so.*"`
            # patterns above are no-ops because double quotes suppress bash glob
            # expansion, so rm searches for the literal name "lib*.so.*" and
            # silently fails (-f). Use an explicit list here instead.
            rm -f ${IMAGE_ROOTFS}/usr/lib/libEGL_adreno.so.1
            rm -f ${IMAGE_ROOTFS}/usr/lib/libEGL.so.1
            rm -f ${IMAGE_ROOTFS}/usr/lib/libGLESv2_adreno.so.2
            rm -f ${IMAGE_ROOTFS}/usr/lib/libGLESv2.so.2
            rm -f ${IMAGE_ROOTFS}/usr/lib/libGLESv1_CM_adreno.so.1
            rm -f ${IMAGE_ROOTFS}/usr/lib/libGLESv1_CM.so.1
            rm -f ${IMAGE_ROOTFS}/usr/lib/libGL.so.1.2.0
            rm -f ${IMAGE_ROOTFS}/usr/lib/libGL.so.1
            rm -f ${IMAGE_ROOTFS}/usr/lib/libq3dtools_adreno.so.1

            # Register the QCom adreno OpenCL ICD so the ocl-icd loader
            # (/usr/lib/aarch64-linux-gnu/libOpenCL.so.1) discovers the adreno
            # vendor library at /usr/lib/libOpenCL_adreno.so.1 instead of falling
            # back to an empty ICD set. This keeps NPU/OpenCL workloads on the
            # QCom stack while GPU/EGL runs on mesa.
            mkdir -p ${IMAGE_ROOTFS}/etc/OpenCL/vendors
            echo "/usr/lib/libOpenCL_adreno.so.1" > ${IMAGE_ROOTFS}/etc/OpenCL/vendors/adreno.icd
        else
            bbfatal "IMAGE_ROOTFS invalid, skip cleanup"
        fi
}       

#ROOTFS_PREPROCESS_COMMAND += "deploy_debian_gnome_rootfs;"
SKIP_DEPLOY_DEBIAN_GNOME_ROOTFS ??= "0"
IMAGE_PREPROCESS_COMMAND:append = "${@'' if d.getVar('SKIP_DEPLOY_DEBIAN_GNOME_ROOTFS') == '1' else ' deploy_debian_gnome_rootfs;'}"
