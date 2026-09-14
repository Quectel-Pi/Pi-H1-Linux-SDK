#!/bin/sh
# Copyright (C) 2026 Quectel Igni.Li
# Licensed on MIT

TOP_DIR=/mnt/top
OTA_INFO_DIR=opt
WORK_SUBVOL=@V0
BACKUP_SUBVOL=@backup
UPGRADE_SCRIPT=/opt/system_upgrade.sh

EFI_KERNEL="/efi/EFI/Linux/linux-qcm6490-idp.efi"
BACKUP_DIR="/opt"
BACKUP_KERNEL="$BACKUP_DIR/linux-qcm6490-idp.efi"
BACKUP_MD5="$BACKUP_DIR/linux-qcm6490-idp.efi.md5"
EFI_KERNEL_BK="/efi/EFI/Linux/linux-qcm6490-idp.bk.efi"
BTRFS_WORKING=0

ORI_KERNEL="/efi/EFI/Linux/linux-qcm6490-idp-original.efi"
ORI_KERNEL_MD5="/efi/EFI/Linux/linux-qcm6490-idp-original.efi.md5"

reboot() {
    command reboot -f "$@"
}

mount_top() {
    if mountpoint -q "$TOP_DIR"; then
        return 0
    fi

    mkdir -p "$TOP_DIR"
    mount -t btrfs /dev/sda3 "$TOP_DIR"
    if [ $? -ne 0 ]; then
        echo "Error: Failed to mount /dev/sda3"
        return 1
    fi
    return 0
}

umount_top() {
    umount $TOP_DIR
}

force_sync(){
    sync
    btrfs filesystem sync /
}

# Boot-success confirmation for A/B fallback.
# Clears the persistent boot-attempt counter and the "unbootable vol" mark
# on the btrfs top-level. Called by:
#   - main()    once the rootfs reaches ota_run running on WORK_SUBVOL
#   - restore_from_backup() before rebooting after a rebuild
# so the (possibly rebuilt) current subvol gets a fresh budget next boot.
boot_confirm() {
    mount_top || return 1

    if [ -f "$TOP_DIR/$OTA_INFO_DIR/boot_attempts" ]; then
        rm -f "$TOP_DIR/$OTA_INFO_DIR/boot_attempts" 2>/dev/null
    fi

    if [ -f "$TOP_DIR/$OTA_INFO_DIR/boot_fail_vol" ]; then
        rm -f "$TOP_DIR/$OTA_INFO_DIR/boot_fail_vol" 2>/dev/null
    fi

    force_sync
    umount_top
}

copy_on_write() {
    SOURCE_PATH="$1"
    DIST_PATH="$2"

    if [ -z "$SOURCE_PATH" ] || [ -z "$DIST_PATH" ]; then
        echo "[ERROR] Missing arguments: SOURCE_PATH DIST_PATH"
        return 1
    fi

    if [ ! -e "$SOURCE_PATH" ]; then
        echo "[ERROR] Source not exist: $SOURCE_PATH"
        return 1
    fi

    DIST_DIR=$(dirname "$DIST_PATH")
    TMP_PATH="${DIST_PATH}.tmp.$$"

    if [ ! -d "$DIST_DIR" ]; then
        mkdir -p "$DIST_DIR" || {
            echo "[ERROR] Failed to create dir: $DIST_DIR"
            return 1
        }
    fi

    echo "[INFO] Calculating source MD5..."
    SRC_MD5=$(md5sum "$SOURCE_PATH" | awk '{print $1}') || {
        echo "[ERROR] Failed to calc source md5"
        return 1
    }

    if cp -a --reflink=auto "$SOURCE_PATH" "$TMP_PATH"; then
        :
    else
        echo "[WARN] reflink copy failed, fallback to normal cp"
        cp -a "$SOURCE_PATH" "$TMP_PATH" || {
            echo "[ERROR] Copy failed"
            return 1
        }
    fi

    sync "$TMP_PATH" 2>/dev/null

    echo "[INFO] Calculating tmp MD5..."
    TMP_MD5=$(md5sum "$TMP_PATH" | awk '{print $1}') || {
        echo "[ERROR] Failed to calc tmp md5"
        rm -rf "$TMP_PATH"
        return 1
    }

    if [ "$SRC_MD5" != "$TMP_MD5" ]; then
        echo "[ERROR] MD5 mismatch!"
        echo "SRC: $SRC_MD5"
        echo "TMP: $TMP_MD5"
        rm -rf "$TMP_PATH"
        return 1
    fi

    echo "[INFO] MD5 verified"

    mv -f "$TMP_PATH" "$DIST_PATH" || {
        echo "[ERROR] Move failed"
        rm -rf "$TMP_PATH"
        return 1
    }

    echo "[INFO] Replace success"
    return 0
}

check_subvol() {
    mount_top || exit 1
    if [ -d "$TOP_DIR/$WORK_SUBVOL" ]; then
        echo "$WORK_SUBVOL exists, no action needed"
        return 0
    else
        echo "$WORK_SUBVOL does not exist, need to create snapshot"
        return 1
    fi
    umount_top
}

init_subvol_snapshot() {
    mkdir -p /opt/kernel_upgrade
    mount_top || return 1

    # backup oring kernel
    echo "[STEP] Backup kernel (copy_on_write)..."

    if [ ! -f "$EFI_KERNEL" ]; then
        echo "Error: kernel not found: $EFI_KERNEL"
        umount_top
        return 1
    fi

    SRC_MD5=$(md5sum "$EFI_KERNEL" | awk '{print $1}')
    if [ -z "$SRC_MD5" ]; then
        echo "Error: failed to calc source md5"
        umount_top
        return 1
    fi

    if ! copy_on_write "$EFI_KERNEL" "$BACKUP_KERNEL"; then
        echo "Error: backup kernel copy failed"
        umount_top
        return 1
    fi

    if ! copy_on_write "$EFI_KERNEL" "$ORI_KERNEL"; then
        echo "Error: emergency kernel copy failed"
        umount_top
        return 1
    fi

    echo "[OK] Kernel backup verified"



    echo "$SRC_MD5" > "$ORI_KERNEL_MD5"

    force_sync

    echo "Creating snapshot $WORK_SUBVOL from top-level"
    btrfs subvolume snapshot $TOP_DIR $TOP_DIR/$WORK_SUBVOL
    if [ $? -ne 0 ]; then
        echo "Error: Failed to create snapshot $WORK_SUBVOL"
        umount_top
        return 1
    fi
    echo "Snapshot $WORK_SUBVOL created successfully"

    force_sync
    umount_top
    reboot
    return 0
}

create_backup_snapshot() {

    cur=$(current_subvol)
    if [ -z "$cur" ]; then
        echo "Not on a Btrfs root filesystem, cannot determine current subvolume"
        return 1
    fi

    echo "Current root subvolume: $cur"

    if [ "$cur" != "/$WORK_SUBVOL" ]; then
        echo "Not currently in $WORK_SUBVOL, skipping backup"
        return 0
    fi

    check_history_flag

    # STEP 0: Backup EFI kernel with MD5 verify

    echo "[STEP] Backing up EFI kernel"

    if [ ! -f "$EFI_KERNEL" ]; then
        echo "Error: EFI kernel not found: $EFI_KERNEL"
        return 1
    fi

    mkdir -p "$BACKUP_DIR"

    SRC_MD5=$(md5sum "$EFI_KERNEL" | awk '{print $1}')
    echo "$SRC_MD5" > "$BACKUP_MD5"

    if ! copy_on_write "$EFI_KERNEL" "$BACKUP_KERNEL"; then
        echo "Error: EFI kernel backup failed"
        return 1
    fi

    echo "EFI kernel backup SUCCESS"

    echo "Creating backup snapshot $BACKUP_SUBVOL from $WORK_SUBVOL"
    mount_top || return 1

    if [ ! -d "$TOP_DIR/$WORK_SUBVOL" ]; then
        echo "Error: $WORK_SUBVOL does not exist, cannot create backup"
        umount_top
        return 1
    fi

    if [ -d "$TOP_DIR/$BACKUP_SUBVOL" ]; then
        echo "Backup $BACKUP_SUBVOL already exists, deleting it"
        btrfs subvolume delete $TOP_DIR/$BACKUP_SUBVOL
        if [ $? -ne 0 ]; then
            echo "Error: Failed to delete existing backup $BACKUP_SUBVOL"
            umount_top
            return 1
        fi
    fi

    btrfs subvolume snapshot $TOP_DIR/$WORK_SUBVOL $TOP_DIR/$BACKUP_SUBVOL
    if [ $? -ne 0 ]; then
        echo "Error: Failed to create backup snapshot $BACKUP_SUBVOL"
        umount_top
        return 1
    fi

    echo "Backup snapshot $BACKUP_SUBVOL created successfully"
    umount_top
    return 0
}

restore_kernel_from_subvol() {  
    #high risk work it will modify kernel image
    #most file work has been repleace by atom work

    TARGET_SUBVOL="$1"

    if [ -z "$TARGET_SUBVOL" ]; then
        echo "Usage: restore_kernel_from_subvol <subvol>"
        return 1
    fi

    if [ "$TARGET_SUBVOL" = "/" ]; then
        echo "[INFO] Using current root filesystem"
        SUBVOL_PATH="/"
    else
        case "$TARGET_SUBVOL" in
            @*) ;;
            *) TARGET_SUBVOL="@$TARGET_SUBVOL" ;;
        esac

        echo "$TARGET_SUBVOL" | grep -Eq '^@[a-zA-Z0-9._-]+$' || {
            echo "Invalid subvol name: $TARGET_SUBVOL"
            return 1
        }

        SUBVOL_PATH="$TOP_DIR/$TARGET_SUBVOL"
    fi

    echo "[EFI RESTORE] From subvolume: $TARGET_SUBVOL"

    mount_top || return 1

    SUBVOL_KERNEL="$SUBVOL_PATH/opt/linux-qcm6490-idp.efi"
    SUBVOL_KERNEL_MD5="$SUBVOL_PATH/opt/linux-qcm6490-idp.efi.md5"

    if [ ! -d "$SUBVOL_PATH" ]; then
        echo "Error: subvolume not found: $SUBVOL_PATH"
        umount_top
        return 1
    fi

    if [ ! -f "$SUBVOL_KERNEL" ] || [ ! -f "$SUBVOL_KERNEL_MD5" ]; then
        echo "Error: backup kernel or md5 not found in subvol"
        umount_top
        return 1
    fi

    SRC_MD5=$(awk '{print $1}' "$SUBVOL_KERNEL_MD5")

    if [ -z "$SRC_MD5" ]; then
        echo "Error: invalid md5 file"
        umount_top
        return 1
    fi


    echo "[EFI RESTORE] Creating backup..."

    rm -f "$EFI_KERNEL_BK"

    ORI_MD5=$(md5sum "$EFI_KERNEL" | awk '{print $1}') || return 1

    cp -f "$EFI_KERNEL" "$EFI_KERNEL_BK" || return 1

    force_sync

    BK_MD5=$(md5sum "$EFI_KERNEL_BK" | awk '{print $1}')

    if [ "$ORI_MD5" != "$BK_MD5" ]; then
        echo "Kernel Backup MD5 mismatch!"
        echo "ORI: $ORI_MD5"
        echo "BK : $BK_MD5"
        rm -f "$EFI_KERNEL_BK"
        return 1
    fi

    echo "[EFI RESTORE] Backup OK"

    MAX_RETRY=3
    RETRY=0
    SUCCESS=0

    while [ $RETRY -lt $MAX_RETRY ]; do
        echo "[EFI RESTORE] Attempt $((RETRY+1))..."

        TMP_NEW="${EFI_KERNEL}.new"
        rm -f "$TMP_NEW"

        dd if="$SUBVOL_KERNEL" of="$TMP_NEW" bs=4M conv=fsync

        DST_MD5=$(md5sum "$TMP_NEW" | awk '{print $1}')

        if [ "$SRC_MD5" != "$DST_MD5" ]; then
            echo "MD5 mismatch on new kernel"
            rm -f "$TMP_NEW"
            continue
        fi

        mv "$TMP_NEW" "$EFI_KERNEL"

        EFI_DIR=$(dirname "$EFI_KERNEL")

        sync "$EFI_DIR"
        force_sync

        if [ ! -f "$EFI_KERNEL" ]; then
            echo "Error: EFI kernel missing after copy"
            RETRY=$((RETRY+1))
            continue
        fi

        DST_MD5=$(md5sum "$EFI_KERNEL" | awk '{print $1}')

        if [ "$SRC_MD5" = "$DST_MD5" ]; then
            echo "EFI kernel restore SUCCESS"
            SUCCESS=1
            rm -f "$EFI_KERNEL_BK"
            force_sync
            break
        else
            echo "MD5 mismatch!"
            echo "SRC: $SRC_MD5"
            echo "DST: $DST_MD5"

            echo "Removing corrupted EFI kernel"
            rm -f "$EFI_KERNEL"
        fi

        RETRY=$((RETRY+1))
    done

    umount_top

    if [ $SUCCESS -ne 1 ]; then
        echo "Error: EFI restore failed after $MAX_RETRY attempts"
        return 1
    fi

    return 0
}


restore_from_backup() {

    cur=$(current_subvol)
    if [ -z "$cur" ]; then
        echo "Not on a Btrfs root filesystem, cannot determine current subvolume"
        return 1
    fi

    echo "Current root subvolume: $cur"

    if [ "$cur" = "/$WORK_SUBVOL" ]; then
        echo "currently in $WORK_SUBVOL, set restore flag"

        mount_top || return 1

        if [ ! -d "$TOP_DIR/$BACKUP_SUBVOL" ]; then
            echo "Error: $BACKUP_SUBVOL does not exist, cannot restore" >&2
            umount_top
            return 1
        fi

        umount_top

        touch /restore_flag
        force_sync

        echo "now will reboot and get into restore mode"
        reboot
        return 0
    fi
    if [ "$cur" != "/$BACKUP_SUBVOL" ]; then
        echo "Not currently in $BACKUP_SUBVOL, skipping restore"
        return 0
    fi

    echo "Restoring $WORK_SUBVOL from $BACKUP_SUBVOL"
    mount_top || return 1

    if [ ! -d "$TOP_DIR/$BACKUP_SUBVOL" ]; then
        echo "Error: Backup subvolume $BACKUP_SUBVOL does not exist"
        umount_top
        return 1
    fi

    echo "[STEP 1] remove old work_subvol /opt"

    if [ -d "$TOP_DIR/$WORK_SUBVOL" ]; then
        echo "Deleting existing $WORK_SUBVOL"
        btrfs subvolume delete $TOP_DIR/$WORK_SUBVOL
        if [ $? -ne 0 ]; then
            echo "Error: Failed to delete $WORK_SUBVOL"
            umount_top
            return 1
        fi
    fi

    echo "[STEP 2] restore work_subvol from backup /opt"
    btrfs subvolume snapshot $TOP_DIR/$BACKUP_SUBVOL $TOP_DIR/$WORK_SUBVOL
    if [ $? -ne 0 ]; then
        echo "Error: Failed to restore $WORK_SUBVOL from backup"
        umount_top
        return 1
    fi

    echo "Restore completed successfully"
    umount_top

    echo "[STEP 3] restore kernel from backup /opt"
    restore_kernel_from_subvol /

    # Give the rebuilt working subvol a fresh A/B budget so the initramfs
    # doesn't immediately fall back to backup again on the next boot.
    boot_confirm 2>/dev/null || true

    reboot
    return 0
}

system_upgrade(){
    NEW_WORK_SUBVOL="$1"

    if [ -z "$NEW_WORK_SUBVOL" ]; then
        echo "[ERROR] Missing new WORK_SUBVOL!"
        return 1
    fi

    case "$NEW_WORK_SUBVOL" in
    @*)
        ;;
    *)
        NEW_WORK_SUBVOL="@$NEW_WORK_SUBVOL"
        ;;
    esac

    echo "$NEW_WORK_SUBVOL" | grep -Eq '^@[a-zA-Z0-9._-]+$' || {
        echo "[ERROR] Invalid subvolume name"
        return 1
    }

    case "$NEW_WORK_SUBVOL" in
        @backup|@backup_*)
            echo "[ERROR] Cannot use backup subvolume name"
            return 1
            ;;
    esac

    echo "[INFO] Target subvolume: $NEW_WORK_SUBVOL"

    if btrfs subvolume list / | awk '{print $NF}' | grep -qx "$NEW_WORK_SUBVOL"; then
        echo "[ERROR] Subvolume '$NEW_WORK_SUBVOL' already exists!"
        return 1
    fi

    echo "[OK] Subvolume name is available"

    echo "========== Start System Upgrade =========="

    cur=$(current_subvol)

    if [ -z "$cur" ]; then
        echo "[ERROR] Cannot determine current subvolume!"
        return 1
    fi

    echo "[INFO] Current subvolume: $cur"

    if [ "$cur" != "/$WORK_SUBVOL" ]; then
        echo "[ERROR] Not running on working subvol $WORK_SUBVOL, abort upgrade!"
        return 1
    fi

    echo "[OK] Running on $WORK_SUBVOL, continue..."

    echo "[STEP 1] Creating backup snapshot... backup working system into $BACKUP_SUBVOL"
    create_backup_snapshot
    ret=$?

    if [ $ret -ne 0 ]; then
        echo "[ERROR] Backup snapshot failed, abort upgrade!"
        return 1
    fi

    echo "[OK] Backup snapshot created successfully"

    mount_top || return 1

    if [ ! -d "$TOP_DIR/$BACKUP_SUBVOL" ]; then
        echo "[ERROR] Backup subvolume not found after creation!"
        umount_top
        return 1
    fi
    umount_top

    echo "[OK] Backup subvolume verified now add restore_flag"

    echo "[STEP 2] Add restore flag so you can roll back while upgrade fail"
    touch /restore_flag
    force_sync
    echo "[STEP 3] Running upgrade script"
    bash "$UPGRADE_SCRIPT"
    ret=$?

    if [ $ret -ne 0 ]; then
        echo "[ERROR] Upgrade script failed (ret=$ret), rebooting for rollback..."
        force_sync
        sleep 2
        reboot
        return 1
    fi

    echo $NEW_WORK_SUBVOL > /version
    force_sync

    # update kernel in same time
    # replace kernel and md5 in opt
    echo "[STEP 4] Update kernel from worksubvol /opt"

    if [ -f /opt/kernel_upgrade/linux-qcm6490-idp.efi ] && \
    [ -f /opt/kernel_upgrade/linux-qcm6490-idp.efi.md5 ] && \
    (cd /opt/kernel_upgrade && md5sum -c linux-qcm6490-idp.efi.md5 >/dev/null 2>&1)
    then

        echo "[INFO] New kernel + MD5 detected, updating..."

        if ! copy_on_write /opt/kernel_upgrade/linux-qcm6490-idp.efi /opt/linux-qcm6490-idp.efi; then
            echo "[ERROR] Kernel update failed, rebooting for rollback..."
            force_sync
            sleep 2
            reboot
            return 1
        fi
        rm -f /opt/kernel_upgrade/linux-qcm6490-idp.efi

        if ! copy_on_write /opt/kernel_upgrade/linux-qcm6490-idp.efi.md5 /opt/linux-qcm6490-idp.efi.md5; then
            echo "[ERROR] Kernel MD5 update failed, rebooting for rollback..."
            force_sync
            sleep 2
            reboot
            return 1
        fi
        rm -f /opt/kernel_upgrade/linux-qcm6490-idp.efi.md5
        
        restore_kernel_from_subvol /
        echo "[OK] Kernel updated successfully"
    else
        echo "[INFO] No valid kernel update found (need both .efi and .md5), skipping"
    fi

    echo "[STEP 5] Add history version flag By $WORK_SUBVOL"
    # 下次开机检查history_ver  当存在history_ver时代表：
    # 1 backup分区为上一版本的历史系统
    # 2 历史版本号对应的subvol，是已升级的旧系统 可以在挂载$NEW_WORK_SUBVOL的情况下删除
    # 操作：
    # 1 下次开机检查history_ver，如果存在，删除历史版本号里面存储名称对应的subvol
    # 2 把backup分区快照生成为以历史版本名称为名的subvol用来回滚旧版本
    # 3 如果backup分区要被覆盖 则脚本先检测flag 如果有history_flag 则先执行1 2 再继续
    # 删除history_ver标记
    mount_top || return 1
    echo "$WORK_SUBVOL" > $TOP_DIR/$OTA_INFO_DIR/history_ver.tmp
    if mv "$TOP_DIR/$OTA_INFO_DIR/history_ver.tmp" "$TOP_DIR/$OTA_INFO_DIR/history_ver"; then
        force_sync
        echo "[OK] history_ver added:$WORK_SUBVOL"
    else
        echo "[ERROR] Failed to add history_ver"
        rm -f "$TOP_DIR/$OTA_INFO_DIR/history_ver.tmp"
        return 1
    fi
    umount_top

    echo "[STEP 6] Update New setting of work subvol to $NEW_WORK_SUBVOL"
    mount_top || return 1
    echo "$NEW_WORK_SUBVOL" > $TOP_DIR/$OTA_INFO_DIR/current_vol.tmp

    if mv "$TOP_DIR/$OTA_INFO_DIR/current_vol.tmp" "$TOP_DIR/$OTA_INFO_DIR/current_vol"; then
        force_sync
        echo "[OK] current_vol updated: next boot WORK_SUBVOL=$NEW_WORK_SUBVOL"
    else
        echo "[ERROR] Failed to update current_vol"
        rm -f "$TOP_DIR/$OTA_INFO_DIR/current_vol.tmp"
        return 1
    fi
    umount_top

    echo "[STEP 7] Upgrade success remove restore_flag on $WORK_SUBVOL"
    rm -f /restore_flag
    force_sync

    echo "[STEP 8] Make New subvol $NEW_WORK_SUBVOL by working subvol"
    mount_top || return 1
    btrfs subvolume snapshot $TOP_DIR/$WORK_SUBVOL $TOP_DIR/$NEW_WORK_SUBVOL
    umount_top
    force_sync

    echo "[STEP 9] Upgrade Finished"
    COUNTDOWN=5
    while [ $COUNTDOWN -gt 0 ]; do
        echo  "Rebooting in $COUNTDOWN seconds...\r"
        sleep 1
        COUNTDOWN=$((COUNTDOWN - 1))
    done

    echo "Reboot now!"
    reboot
}

check_history_flag () {
    mount_top || return 1

    if [ -f "$TOP_DIR/$OTA_INFO_DIR/history_ver" ]; then
        echo "Reading $TOP_DIR/$OTA_INFO_DIR/history_ver..."

        HISTORY_SUBVOL=$(tr -d '\r\n' < "$TOP_DIR/$OTA_INFO_DIR/history_ver" | sed 's/^ *//;s/ *$//')

        if [ -z "$HISTORY_SUBVOL" ]; then
            echo "Error: history_ver is empty!"
        else
            echo "HISTORY_SUBVOL=$HISTORY_SUBVOL"
            echo "remove last subvol"
            if [ -d "$TOP_DIR/$HISTORY_SUBVOL" ]; then
                echo "History $HISTORY_SUBVOL  exists, deleting it"
                btrfs subvolume delete $TOP_DIR/$HISTORY_SUBVOL
                if [ $? -ne 0 ]; then
                    echo "Error: Failed to delete existing history-vol $HISTORY_SUBVOL"
                    umount_top
                    return 1
                fi
            fi
            echo "Finished remove last subvol,Now restore it from backup"
            btrfs subvolume snapshot $TOP_DIR/$BACKUP_SUBVOL $TOP_DIR/$HISTORY_SUBVOL
            if [ $? -ne 0 ]; then
                echo "Error: Failed to restore $HISTORY_SUBVOL from backup"
                umount_top
                return 1
            fi 

            rm -f $TOP_DIR/$OTA_INFO_DIR/history_ver
            sync
        fi
    else
        echo "info: $TOP_DIR/$OTA_INFO_DIR/history_ver not found skip now"
        umount_top
        return 0
    fi

    umount_top
}


current_subvol() {
    info=$(mount | grep "on / " | grep btrfs)
    if [ -z "$info" ]; then
        return 1
    fi
    subvol=$(echo "$info" | sed -n 's/.*subvol=\([^,)]*\).*/\1/p')
    echo "$subvol"
}

delete_subvol(){
    TARGET_SUBVOL="$1"

    if [ -z "$TARGET_SUBVOL" ]; then
        echo "[ERROR] Missing subvolume name!"
        return 1
    fi

    case "$TARGET_SUBVOL" in
    @*)
        ;;
    *)
        TARGET_SUBVOL="@$TARGET_SUBVOL"
        ;;
    esac

    case "$TARGET_SUBVOL" in
        @backup|@backup_*)
            echo "[ERROR] Cannot delete backup subvolume"
            return 1
            ;;
    esac

    echo "[INFO] Target subvolume: $TARGET_SUBVOL"

    if [ "$TARGET_SUBVOL" = "@backup" ]; then
        echo "[ERROR] Cannot delete @backup subvolume!"
        return 1
    fi

    CURRENT_SUBVOL=$(current_subvol)

    if [ -z "$CURRENT_SUBVOL" ]; then
        echo "[ERROR] Cannot determine current subvolume!"
        return 1
    fi

    CURRENT_SUBVOL="${CURRENT_SUBVOL#/}"

    case "$CURRENT_SUBVOL" in
        @*) ;;
        *) CURRENT_SUBVOL="@$CURRENT_SUBVOL" ;;
    esac

    echo "[INFO] Current active subvolume: $CURRENT_SUBVOL"

    if [ "$TARGET_SUBVOL" = "$CURRENT_SUBVOL" ]; then
        echo "[ERROR] Cannot delete current running subvolume!"
        return 1
    fi

    mount_top || return 1

    if ! btrfs subvolume list $TOP_DIR/ | awk '{print $NF}' | grep -qx "$TARGET_SUBVOL"; then
        echo "[ERROR] Subvolume '$TARGET_SUBVOL' does not exist!"
        umount_top
        return 1
    fi

    echo "[INFO] Deleting subvolume: $TARGET_SUBVOL"

    if btrfs subvolume delete "$TOP_DIR/$TARGET_SUBVOL"; then
        echo "[OK] Subvolume deleted successfully"
        umount_top
        return 0
    else
        echo "[ERROR] Failed to delete subvolume!"
        umount_top
        return 1
    fi
}

main() {

    btrfs filesystem resize max /  
    cur=$(current_subvol)
    echo "Current root subvolume: $cur"

    if [ "$cur" = "/$BACKUP_SUBVOL" ]; then
        echo "Detected running on $BACKUP_SUBVOL, starting restore..."
        restore_from_backup
        return
    fi

    check_subvol
    if [ $? -ne 0 ]; then
        #init_subvol_snapshot
        #remove auto init here，customer run ota_run init first
        echo "Skip init btrfs auto backup ,run ota_run init first"
    else
        check_history_flag
        # We reached ota_run running on the working subvol, which means the
        # boot succeeded: clear the A/B fallback counter so the current
        # subvol keeps its full retry budget next time.
        boot_confirm 2>/dev/null || true
    fi
}

check_work_dir_setting() {
    mount_top || exit 1
    if [ -f "$TOP_DIR/$OTA_INFO_DIR/current_vol" ]; then
        echo "Reading $TOP_DIR/$OTA_INFO_DIR/current_vol..." >/dev/console

        WORK_SUBVOL=$(tr -d '\r\n' < "$TOP_DIR/$OTA_INFO_DIR/current_vol" | sed 's/^ *//;s/ *$//')

        if [ -z "$WORK_SUBVOL" ]; then
            echo "Error: current_vol is empty!" >/dev/console
        else
            echo "WORK_SUBVOL=$WORK_SUBVOL" >/dev/console
        fi
    else
        echo "Warning: $TOP_DIR/$OTA_INFO_DIR/current_vol not found, using default subvolumes" >/dev/console
    fi
    umount_top
}

list_all_subvol() {
    mount_top || exit 1
    btrfs subvolume list $TOP_DIR
    umount_top
}

roll_back_to_subvol(){
    TARGET_SUBVOL="$1"

    if [ -z "$TARGET_SUBVOL" ]; then
        echo "Usage: roll_back_to_subvol <subvol>"
        return 1
    fi

    case "$TARGET_SUBVOL" in
        @*) ;;
        *) TARGET_SUBVOL="@$TARGET_SUBVOL" ;;
    esac

    echo "$TARGET_SUBVOL" | grep -Eq '^@[a-zA-Z0-9._-]+$' || {
        echo "Invalid subvol name"
        return 1
    }

    case "$TARGET_SUBVOL" in
        *@backup*|@backup)
            echo "Error: rollback to backup subvolume is not allowed"
            return 1
            ;;
    esac

    mount_top || return 1

    SUBVOL_PATH="$TOP_DIR/$TARGET_SUBVOL"

    if [ ! -d "$SUBVOL_PATH" ]; then
        echo "[ERROR] Subvolume not found: $TARGET_SUBVOL"
        umount_top
        return 1
    fi

    btrfs subvolume show "$SUBVOL_PATH" >/dev/null 2>&1 || {
        echo "[ERROR] Not a valid btrfs subvolume: $TARGET_SUBVOL"
        umount_top
        return 1
    }

    ###update kernel if you need
    ###update kernel if you need
    restore_kernel_from_subvol $TARGET_SUBVOL
    echo "Kernel Rollback done,now switch rootfs flag"

    mount_top || return 1
    echo "$TARGET_SUBVOL" > "$TOP_DIR/$OTA_INFO_DIR/current_vol.tmp"

    if mv "$TOP_DIR/$OTA_INFO_DIR/current_vol.tmp" \
          "$TOP_DIR/$OTA_INFO_DIR/current_vol"; then
        sync
        echo "[OK] current_vol updated: next boot WORK_SUBVOL=$TARGET_SUBVOL"
    else
        echo "[ERROR] Failed to update current_vol"
        rm -f "$TOP_DIR/$OTA_INFO_DIR/current_vol.tmp"
        umount_top
        return 1
    fi

    umount_top

    echo "[OK] Rollback Finished"
    COUNTDOWN=5
    while [ $COUNTDOWN -gt 0 ]; do
        echo  "Rebooting in $COUNTDOWN seconds...\r"
        sleep 1
        COUNTDOWN=$((COUNTDOWN - 1))
    done

    echo  "Reboot now!"
    reboot
}

on_interrupt() {
    trap '' INT TERM

    echo ""
    echo "[WARN] Interrupted by user"
    echo "[WARN] System will reboot to keep state consistent"

    touch /restore_flag

    sync
    sleep 1

    echo "[INFO] Rebooting..."
    reboot || poweroff -f
}

trap on_interrupt INT TERM

FCT_FLAG="/var/persist/fct_done_flag"
if [ ! -f "$FCT_FLAG" ]; then
    echo "[ERROR] System not ready. Please complete factory initialization first."
    exit 1
fi

check_work_dir_setting

cur_subvol=$(current_subvol)

if [ -z "$cur_subvol" ]; then
    BTRFS_WORKING=0
    return 0
fi

cur_subvol="${cur_subvol#/}"

if [ "$cur_subvol" = "/" ] || [ -z "$cur_subvol" ]; then
    echo "[INFO] Running on root filesystem (/)"
    BTRFS_WORKING=0
else
    BTRFS_WORKING=1
fi


if [ $# -gt 0 ]; then

    if [ "$1" = "init" ]; then
        if [ "${BTRFS_WORKING:-0}" = "1" ]; then
            echo "BTRFS backup function has already inited, now running in $WORK_SUBVOL"
        else
            init_subvol_snapshot
        fi
        exit 0
    fi

    if [ "${BTRFS_WORKING:-0}" != "1" ]; then
        echo "[ERROR] BTRFS is not initialized. Please run: $0 init"
        exit 1
    fi

    case "$1" in
        backup)
            create_backup_snapshot
            ;;
        current)
            current_subvol
            ;;
        restore)
            restore_from_backup
            ;;
        list)
            list_all_subvol
            ;;
        upgrade)
            if [ $# -lt 2 ]; then
                echo "Usage: $0 upgrade <@your_new_vol_name>"
                exit 1
            fi
            system_upgrade "$2"
            ;;
        rollback)
            if [ $# -lt 2 ]; then
                echo "Usage: $0 rollback <@your_rollback_vol>"
                exit 1
            fi
            roll_back_to_subvol "$2"
            ;;
        delete)
            if [ $# -lt 2 ]; then
                echo "Usage: $0 delete <@your_new_vol_name>"
                exit 1
            fi
            delete_subvol "$2"
            ;;
        *)
            echo "Usage: $0 {init|backup|current|restore|list|rollback <@vol>|upgrade <@vol>|delete <@vol>}"
            exit 1
            ;;
    esac
else
    main
fi