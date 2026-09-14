#!/bin/bash
set -e

DEFAULT_NAME="ubuntu26-gnome-rootfs"

echo
read -rp "Enter rootfs directory name [default: ${DEFAULT_NAME}]: " ROOTFS_DIR
ROOTFS_DIR="${ROOTFS_DIR:-$DEFAULT_NAME}"

ROOTFS_TAR="${ROOTFS_DIR}.tar.xz"

echo "[INFO] Using rootfs dir: ${ROOTFS_DIR}"
echo "[INFO] Using rootfs tar: ${ROOTFS_TAR}"
echo

# ---------- 1. rootfs 存在性检查 ----------
if [ ! -d "$ROOTFS_DIR" ]; then
    echo "[INFO] $ROOTFS_DIR not found, extracting rootfs..."

    if [ ! -f "$ROOTFS_TAR" ]; then
        echo "[ERROR] $ROOTFS_TAR not found"
        exit 1
    fi

    mkdir -p "$ROOTFS_DIR"
    tar --numeric-owner --same-permissions -xJf "$ROOTFS_TAR" -C "$ROOTFS_DIR"
    echo "[INFO] rootfs extracted"
else
    echo "[INFO] $ROOTFS_DIR already exists, skip extract"
fi

# ---------- 2. 启用 qemu binfmt ----------
sudo update-binfmts --enable qemu-aarch64

# ---------- 3. 挂载 ----------
mkdir -p "$ROOTFS_DIR/dev"
mkdir -p "$ROOTFS_DIR/dev/pts"
mkdir -p "$ROOTFS_DIR/proc"
mkdir -p "$ROOTFS_DIR/sys"
mkdir -p "$ROOTFS_DIR/tmp"

sudo mount --bind /dev     "$ROOTFS_DIR/dev"
sudo mount --bind /dev/pts "$ROOTFS_DIR/dev/pts"
sudo mount --bind /proc    "$ROOTFS_DIR/proc"
sudo mount --bind /sys     "$ROOTFS_DIR/sys"
sudo mount --bind /tmp     "$ROOTFS_DIR/tmp"

# ---------- 4. 进入 chroot ----------
sudo chroot "$ROOTFS_DIR" /usr/bin/qemu-aarch64-static /bin/bash   --norc -i

# ---------- 5. 退出后卸载 ----------
# Try normal umount first; fall back to lazy umount if busy.
for mp in "$ROOTFS_DIR/dev/pts" "$ROOTFS_DIR/dev" "$ROOTFS_DIR/proc" "$ROOTFS_DIR/sys" "$ROOTFS_DIR/tmp"; do
    if mountpoint -q "$mp"; then
        sudo umount "$mp" 2>/dev/null || sudo umount -l "$mp" || true
    fi
done

# ---------- 6. 检查残留挂载 ----------
if mount | grep -q " $ROOTFS_DIR"; then
    echo "[ERROR] mounts still present under $ROOTFS_DIR — refusing to repack:"
    mount | grep " $ROOTFS_DIR"
    exit 1
else
    echo "[INFO] no mount left"
fi

# ---------- 7. 是否重新打包 ----------
echo
read -rp "Repack ${ROOTFS_DIR} with date suffix? [Y/N]: " ANSWER

if [[ "$ANSWER" =~ ^[Yy]$ ]]; then
    DATE_TAG=$(date +%Y%m%d-%H%M)
    OUT_TAR="${ROOTFS_DIR}-${DATE_TAG}.tar.xz"

    # Multi-threaded xz: -T0 = all cores, -6 = default level (balance speed/size).
    # Drop to -3 for ~2-3x more speed at ~10% larger size; raise to -9 only for archival.
    XZ_LEVEL="${XZ_LEVEL:-6}"
    XZ_THREADS="${XZ_THREADS:-0}"   # 0 = auto-detect cores

    NPROC=$(nproc 2>/dev/null || echo 1)
    echo "[INFO] repacking rootfs -> $OUT_TAR"
    echo "[INFO] xz: level=${XZ_LEVEL}, threads=${XZ_THREADS} (host has ${NPROC} cores)"

    sudo tar --numeric-owner \
    	--one-file-system \
    	--exclude='./dev/*' \
    	--exclude='./proc/*' \
    	--exclude='./sys/*' \
    	--exclude='./run/*' \
    	--exclude='./tmp/*' \
    	--exclude='./var/cache/apt/archives/*.deb' \
    	--exclude='swapfile' \
    	--exclude='*.iso' \
    	-I "xz -T${XZ_THREADS} -${XZ_LEVEL}" \
    	-cf "$OUT_TAR" \
    	-C "$ROOTFS_DIR" .
    #Timbo: --exclude after the source path is ignored
    #sudo tar --numeric-owner -cJf "$OUT_TAR" \
    #    -C "$ROOTFS_DIR" . \
    #    --exclude='swapfile' \
    #    --exclude='*.iso'

    echo "[INFO] repack finished: $OUT_TAR"
else
    echo "[INFO] skip repack"
fi

# ---------- 8. 选择 tar.xz 作为默认 rootfs ----------
echo
echo "Available tar.xz packages:"
TAR_LIST=($(ls -1 *.tar.xz 2>/dev/null || true))

if [ "${#TAR_LIST[@]}" -eq 0 ]; then
    echo "[INFO] no tar.xz packages found, skip rename"
else
    for i in "${!TAR_LIST[@]}"; do
        printf "  [%d] %s\n" "$((i+1))" "${TAR_LIST[$i]}"
    done

    echo
    read -rp "Select a package number to set as ${ROOTFS_TAR} (N to skip): " SELECT

    if [[ "$SELECT" =~ ^[Nn]$ ]]; then
        echo "[INFO] skip rename"
    elif [[ "$SELECT" =~ ^[0-9]+$ ]] && [ "$SELECT" -ge 1 ] && [ "$SELECT" -le "${#TAR_LIST[@]}" ]; then
        CHOSEN="${TAR_LIST[$((SELECT-1))]}"

        # 如果目标已存在，先备份
        if [ -f "$ROOTFS_TAR" ]; then
            BACKUP="${ROOTFS_TAR}.bak.$(date +%Y%m%d-%H%M%S)"
            mv "$ROOTFS_TAR" "$BACKUP"
            echo "[INFO] existing $ROOTFS_TAR backed up as $BACKUP"
        fi

        mv "$CHOSEN" "$ROOTFS_TAR"
        echo "[INFO] $CHOSEN -> $ROOTFS_TAR"
    else
        echo "[WARN] invalid input, skip rename"
    fi
fi