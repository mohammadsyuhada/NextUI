#!/bin/sh
cd "$(dirname "$0")"

# Lock CPU at 2GHz for video decoding — userspace governor with fixed frequency
# has no scaling overhead and ensures max performance from the first frame
echo userspace > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null
echo 2000000 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_setspeed 2>/dev/null

export SDL_NOMOUSE=1
export HOME=/mnt/SDCARD

# Create Videos directory if it doesn't exist
mkdir -p /mnt/SDCARD/Videos

./mediaplayer.elf &> "$LOGS_PATH/media-player.txt"

# Restore default governor on exit
echo ondemand > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null
