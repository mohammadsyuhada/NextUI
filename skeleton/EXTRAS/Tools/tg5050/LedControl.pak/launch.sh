#!/bin/sh
cd $(dirname "$0")

TARGET_PATH="/mnt/SDCARD/.userdata/shared/ledsettings.txt"
if [ ! -f "$TARGET_PATH" ]; then
    cp ./ledsettings.txt "$TARGET_PATH"
fi

./ledcontrol.elf &> "$LOGS_PATH/ledcontrol.txt"
