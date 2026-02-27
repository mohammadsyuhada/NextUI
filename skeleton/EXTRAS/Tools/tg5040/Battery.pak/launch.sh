#!/bin/sh

cd $(dirname "$0")

# Low fixed frequency for simple UI
echo 600000 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq 2>/dev/null

./battery.elf &> "$LOGS_PATH/battery.txt"
