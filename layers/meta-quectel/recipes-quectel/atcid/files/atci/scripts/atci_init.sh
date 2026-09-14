#!/bin/sh

while [ ! -e /dev/ttyGS0 ]; do
    echo "Waiting for /dev/ttyGS0..."
    sleep 1
done

# pi 用户需要访问 /dev/dma_heap/qcom,audio-ml 才能启动 pipewire(AGM init)。
# 默认权限 660 (system:audio)，pi 不在该组，AGM init 失败导致 pipewire 起不来，
# 故统一放开 dma_heap 设备权限。
chmod 666 /dev/dma_heap/* 2>/dev/null || true

exec /usr/sbin/atcid
