#!/bin/sh
cd "$(dirname "$0")"

# Set CPU frequency for music player (power saving: 672 MHz)
echo userspace > /sys/devices/system/cpu/cpu4/cpufreq/scaling_governor 2>/dev/null
echo 672000 > /sys/devices/system/cpu/cpu4/cpufreq/scaling_setspeed 2>/dev/null

# Create Music directory if it doesn't exist
mkdir -p /mnt/SDCARD/Music

./musicplayer.elf &> "$LOGS_PATH/music-player.txt"

# Restore default CPU governor on exit
echo ondemand > /sys/devices/system/cpu/cpu4/cpufreq/scaling_governor 2>/dev/null
