#!/bin/sh
cd $(dirname "$0")

# remove original leddaemon
LCDAEMON_PATH="/etc/LedControl"
rm -R $LCDAEMON_PATH 2> /dev/null
if [ -f /etc/init.d/lcservice ]; then
    /etc/init.d/lcservice disable
    rm /etc/init.d/lcservice 2> /dev/null
fi

cd $(dirname "$0")

TARGET_PATH="/mnt/SDCARD/.userdata/shared/ledsettings.txt"
if [ ! -f "$TARGET_PATH" ]; then
    cp ./ledsettings.txt "$TARGET_PATH"
fi

TARGET_PATH="/mnt/SDCARD/.userdata/shared/ledsettings_brick.txt"
if [ ! -f "$TARGET_PATH" ]; then
    cp ./ledsettings_brick.txt "$TARGET_PATH"
fi

./ledcontrol.elf &> "$LOGS_PATH/ledcontrol.txt"
