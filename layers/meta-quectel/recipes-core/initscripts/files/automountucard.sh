#!/bin/sh
# 记录日志到/var/log/ucard-mount.log
LOG="/var/log/ucard-mount.log"

PORT=$1
DEVICE=$2
MOUNT_POINT="/mnt/sdcard$PORT"

echo "$(date) - Processing $DEVICE on port $PORT" >> $LOG

case $ACTION in
  add)
    mkdir -p $MOUNT_POINT
    if mount "/dev/$DEVICE" $MOUNT_POINT; then
      echo "$(date) - Mounted $DEVICE to $MOUNT_POINT" >> $LOG
    else
      echo "$(date) - Failed to mount $DEVICE" >> $LOG
      rmdir $MOUNT_POINT
    fi
    ;;
  remove)
    if umount $MOUNT_POINT; then
      echo "$(date) - Unmounted $MOUNT_POINT" >> $LOG
      rmdir $MOUNT_POINT
    fi
    ;;
esac
