#!/bin/sh
cd "$(dirname "$0")"

# Use ondemand governor — scales frequency dynamically based on load
echo ondemand > /sys/devices/system/cpu/cpu4/cpufreq/scaling_governor 2>/dev/null
echo 672000 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_min_freq
echo 1680000 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq

# Create Music directory if it doesn't exist
mkdir -p /mnt/SDCARD/Music

./musicplayer.elf &> "$LOGS_PATH/music-player.txt"

# Restore default CPU settings on exit
echo 2160000 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq