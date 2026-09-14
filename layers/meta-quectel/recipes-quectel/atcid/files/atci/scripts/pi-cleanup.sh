#!/bin/sh
FLAG_FILE="/var/persist/fct_done_flag"
USER_NAME="pi"
MAX_RETRY=3
SLEEP_BETWEEN=1
LOGIND_SERVICE="/lib/systemd/system/systemd-logind.service"
LOGIND_WANTS="/etc/systemd/system/multi-user.target.wants/systemd-logind.service"

PERSIST_DEV="/dev/sda2"
PERSIST_MNT="/var/persist"
BACKUP_DIR="/opt/persist_backup"

log() {
    echo "[pi-setup] $*"
}

[ -d /var ] || exit 0


if [ -f "$FLAG_FILE" ]; then
    if id "$USER_NAME" >/dev/null 2>&1; then
        PASS_STATUS=$(passwd -S "$USER_NAME" 2>/dev/null | awk '{print $2}')
        if [ "$PASS_STATUS" = "NP" ] || [ "$PASS_STATUS" = "L" ]; then
            log "Deleting user $USER_NAME ..."
            pkill -9 -u "$USER_NAME" 2>/dev/null

            i=1
            while [ "$i" -le "$MAX_RETRY" ]; do
                userdel -r "$USER_NAME" 2>/dev/null && break
                log "userdel failed (try $i/$MAX_RETRY), retrying in ${SLEEP_BETWEEN}s..."
                sleep "$SLEEP_BETWEEN"
                pkill -9 -u "$USER_NAME" 2>/dev/null
                rm -rf /home/pi/
                i=$((i + 1))
            done

            if id "$USER_NAME" >/dev/null 2>&1; then
                log "Failed to delete $USER_NAME after $MAX_RETRY attempts."
            else
                rm -f /etc/gdm3/daemon.conf /etc/gdm3/custom.conf
                log "$USER_NAME deleted and GDM configs removed."
            fi
        else
            log "$USER_NAME exists with valid password, keeping user."
        fi
    else
        log "$USER_NAME does not exist, nothing to do."
    fi
else
    log "FCT flag not found — disabling systemd-logind..."

    i=1
    while [ "$i" -le 30 ]; do
        if systemctl list-units --type=service --state=running 2>/dev/null | grep -q systemd-logind.service; then
            log "Stopping systemd-logind..."
            systemctl stop systemd-logind 2>/dev/null || true
            break
        fi
        sleep 0.5
        i=$((i + 1))
    done

    log "Done waiting (either stopped or never started)."
fi


sync_persist_backup() {
    echo "[persist] checking bidirectional backup..."

    mkdir -p "$PERSIST_MNT"
    mkdir -p "$BACKUP_DIR"

    PERSIST_FILE="$PERSIST_MNT/mac_addr"
    BACKUP_FILE="$BACKUP_DIR/mac_addr"

    if [ -f "$PERSIST_FILE" ] && [ ! -f "$BACKUP_FILE" ]; then
        echo "[persist] persist exists, backup missing -> copying to backup"
        cp -af "$PERSIST_MNT/"* "$BACKUP_DIR/" 2>/dev/null
    fi

    if [ -f "$BACKUP_FILE" ] && [ ! -f "$PERSIST_FILE" ]; then
        echo "[persist] backup exists, persist missing -> restoring to persist"
        cp -af "$BACKUP_DIR/"* "$PERSIST_MNT/" 2>/dev/null
    fi

    echo "[persist] sync done"
}

repair_and_mount_persist() {
    echo "[persist] checking mount..."

    mkdir -p "$PERSIST_MNT"

    mountpoint -q "$PERSIST_MNT" 2>/dev/null
    if [ $? -eq 0 ]; then
        echo "[persist] already mounted"
        sync_persist_backup
        return 0
    fi

    mount "$PERSIST_DEV" "$PERSIST_MNT" 2>/dev/null
    if [ $? -eq 0 ]; then
        echo "[persist] mount success"
        sync_persist_backup
        return 0
    fi

    echo "[persist] mount failed, checking filesystem..."

    FSTYPE=$(blkid -o value -s TYPE "$PERSIST_DEV" 2>/dev/null)

    if [ "$FSTYPE" != "ext4" ]; then
        echo "[persist] no valid ext4, formatting..."
        mkfs.ext4 -F -L persist "$PERSIST_DEV"
    else
        echo "[persist] ext4 exists but mount failed, running fsck..."
        fsck.ext4 -y "$PERSIST_DEV"
    fi

    echo "[persist] retry mount..."
    mount "$PERSIST_DEV" "$PERSIST_MNT"

    if [ $? -ne 0 ]; then
        echo "[persist] ERROR: mount still failed"
        return 1
    fi

    echo "[persist] repair success"

    sync_persist_backup
    return 0
}

btrfs filesystem resize max /
mkdir -p /efi
mount /dev/sda1 /efi

chmod 666 /dev/aud_pasthru_adsp
chmod 666 /dev/dma_heap/*

echo heartbeat > /sys/class/leds/green/trigger

repair_and_mount_persist

refresh_icon_caches() {
    echo "[pi-setup] refreshing icon and desktop caches..."

    if command -v update-desktop-database >/dev/null 2>&1; then
        update-desktop-database -q /usr/share/applications 2>/dev/null
    fi

    if command -v gtk-update-icon-cache >/dev/null 2>&1; then
        for icondir in /usr/share/icons/*; do
            [ -d "$icondir" ] || continue
            [ -f "$icondir/index.theme" ] || continue
            gtk-update-icon-cache -fq "$icondir" 2>/dev/null
        done
    else
        find /usr/share/icons -maxdepth 2 -name icon-theme.cache -delete 2>/dev/null
    fi

    echo "[pi-setup] icon and desktop caches refreshed"
}

refresh_icon_caches

# Restore dump mode from /etc/qpi-config/qpi-config.ini (dumpenable field,
# written by AT+QCFG="dumpenable"). dumpenable=1 => full dump; 0 => off.
# We deliberately do NOT use /var/persist/dump_flag: the persist partition
# has a whole-directory bidirectional backup (sync_persist_backup) which
# resurrects deleted flag files, so dump could never be turned off on
# performance builds. qpi-config.ini lives on the rootfs and is untouched
# by that backup.
DUMP_INI="/etc/qpi-config/qpi-config.ini"
DLOAD_MODE="/sys/module/qcom_scm/parameters/download_mode"
if [ -e "$DLOAD_MODE" ]; then
    if [ -f "$DUMP_INI" ] && grep -q '^dumpenable=1' "$DUMP_INI"; then
        echo "[dump] dumpenable=1, enabling dump (full)"
        echo full > "$DLOAD_MODE" 2>/dev/null
    else
        echo "[dump] dumpenable=0, disabling dump (off)"
        echo off > "$DLOAD_MODE" 2>/dev/null
    fi
else
    echo "[dump] sysfs node missing, skip"
fi

exit 0
