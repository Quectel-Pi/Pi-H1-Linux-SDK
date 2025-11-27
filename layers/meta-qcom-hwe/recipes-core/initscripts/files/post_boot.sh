#!/bin/sh
# Copyright (c) 2023-2024, Qualcomm Innovation Center, Inc. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause-Clear

for policy in `ls -d /sys/devices/system/cpu/cpufreq/policy*`;
    do echo schedutil > $policy/scaling_governor;
done

echo 128 > /proc/sys/kernel/sched_util_clamp_min_rt_default

echo s2idle > /sys/power/mem_sleep
echo 100 > /proc/sys/vm/swappiness
# Disable periodic kcompactd wakeups. We do not use THP, so having many
# huge pages is not as necessary.
echo 0 > /proc/sys/vm/compaction_proactiveness

. /etc/quecpi_config/quecpi_config.ini

echo $led_red_trigger > /sys/class/leds/red/trigger
echo $led_blue_trigger > /sys/class/leds/blue/trigger
echo $led_green_trigger > /sys/class/leds/green/trigger
echo $led_red_brightness > /sys/class/leds/red/brightness
echo $led_blue_brightness > /sys/class/leds/blue/brightness
echo $led_green_brightness > /sys/class/leds/green/brightness
