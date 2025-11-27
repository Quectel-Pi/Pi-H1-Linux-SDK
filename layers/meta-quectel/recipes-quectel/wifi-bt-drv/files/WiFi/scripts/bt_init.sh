#!/bin/bash
killall -9 hciattach
sleep 1
echo 0 > /sys/devices/platform/rfkill/bt_en
sleep 1
echo 1 > /sys/devices/platform/rfkill/bt_en
sleep 1
hciattach /dev/ttyHS1 qca -t120 3000000 flow
hciconfig hci0 up