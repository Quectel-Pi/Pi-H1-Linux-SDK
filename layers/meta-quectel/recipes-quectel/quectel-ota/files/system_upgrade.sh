#!/bin/sh
# Copyright (C) 2026 Quectel Igni.Li
# Licensed on MIT

# Step 1 Put your update command here
# apt update && apt upgrade

# Step 2 Update kernel
# if you need update your kernel by this script.put your kernel file and md5sum of kernel file into /opt
# /opt/linux-qcm6490-idp.efi"
# /opt/linux-qcm6490-idp.efi.md5"
# YOU NEED TO ENSURE YOUR KERNEL IS ENABLE TO WORK BY YOUR TEST AND ADD MD5 FILE ,OR KERNEL UPGRADE WILL NOT RUN
# if you dont want upgrade kernel, see "system_upgrade" function  STEP 7 in /usr/sbin/ota_run 

echo "This Is A Demo Script for Upgrade! Please read /opt/system_upgrade.sh"
echo "This Is A Demo Script for Upgrade! Please read /opt/system_upgrade.sh"
echo "This Is A Demo Script for Upgrade! Please read /opt/system_upgrade.sh"

wait_for_network() {
    echo "[INFO] Waiting for network..."

    while true; do
        ping -c 1 -W 1 8.8.8.8 >/dev/null 2>&1

        if [ $? -eq 0 ]; then
            echo "[INFO] Network is UP"
            break
        fi

        echo "[WAIT] Network not ready... $(date '+%Y-%m-%d %H:%M:%S')"

        sleep 2
    done
}


sync_time() {
    echo "[INFO] Syncing time with NTP..."

    if command -v timedatectl >/dev/null 2>&1; then
        timedatectl set-ntp true
        sleep 2
        timedatectl status
        return
    fi

    if command -v ntpdate >/dev/null 2>&1; then
        ntpdate pool.ntp.org
        return
    fi

    if command -v ntpd >/dev/null 2>&1; then
        ntpd -n -q -p pool.ntp.org
        return
    fi

    echo "[WARN] No NTP tool found, skip time sync"
}


echo "====================================="
echo " This Is A Demo Script for Upgrade!"
echo "====================================="

wait_for_network

sync_time

echo "[INFO] Running apt update..."
apt update

echo "[INFO] Running apt upgrade..."
apt upgrade -y