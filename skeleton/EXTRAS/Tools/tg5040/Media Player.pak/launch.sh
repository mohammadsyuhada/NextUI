#!/bin/sh
cd "$(dirname "$0")"

# Use ondemand governor — scales frequency dynamically based on load
echo ondemand > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null

export SDL_NOMOUSE=1
export HOME=/mnt/SDCARD

# Create Videos directory if it doesn't exist
mkdir -p /mnt/SDCARD/Videos

./mediaplayer.elf &> "$LOGS_PATH/media-player.txt"
