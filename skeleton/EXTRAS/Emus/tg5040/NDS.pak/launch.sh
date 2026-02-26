#!/bin/sh
set -eo pipefail
set -x

rm -f "$LOGS_PATH/NDS.txt"
exec >>"$LOGS_PATH/NDS.txt"
exec 2>&1

echo "$0" "$@"

EMU_DIR="$SDCARD_PATH/Emus/shared/Drastic"
PAK_DIR="$(dirname "$0")"

# libadvdrastic.so (display/input hook) needs libjson-c.so.4 but device may ship libjson-c.so.5
if [ ! -f "$EMU_DIR/libs/libjson-c.so.4" ] && [ -f /usr/lib/libjson-c.so.5 ]; then
    mkdir -p "$EMU_DIR/libs"
    cp /usr/lib/libjson-c.so.5 "$EMU_DIR/libs/libjson-c.so.4"
fi

export PATH="$EMU_DIR:$PATH"
# NDS.pak libs first (custom SDL2, libadvdrastic), then shared libs
export LD_LIBRARY_PATH="$PAK_DIR/libs:$EMU_DIR/libs:$LD_LIBRARY_PATH"

cleanup() {
    rm -f /tmp/stay_awake
    umount "$EMU_DIR/config" 2>/dev/null || true
    umount "$EMU_DIR/backup" 2>/dev/null || true
    umount "$EMU_DIR/cheats" 2>/dev/null || true
    umount "$EMU_DIR/savestates" 2>/dev/null || true
}

main() {
    echo "1" >/tmp/stay_awake
    trap "cleanup" EXIT INT TERM HUP QUIT

    # Setup external directories
    mkdir -p "$SDCARD_PATH/Saves/NDS"
    mkdir -p "$SDCARD_PATH/Cheats/NDS"
    mkdir -p "$SHARED_USERDATA_PATH/NDS-advanced-drastic/config"
    mkdir -p "$EMU_DIR/backup"
    mkdir -p "$EMU_DIR/cheats"
    mkdir -p "$EMU_DIR/config"
    mkdir -p "$EMU_DIR/savestates"

    # Apply device-specific config (Brick vs Smart Pro)
    if [ "$DEVICE" = "brick" ]; then
        DEVICE_CONFIG="$PAK_DIR/devices/trimui-brick/config"
    else
        DEVICE_CONFIG="$PAK_DIR/devices/trimui-smart-pro/config"
    fi
    if [ -d "$DEVICE_CONFIG" ]; then
        cp "$DEVICE_CONFIG/drastic.cfg" "$SHARED_USERDATA_PATH/NDS-advanced-drastic/config/" 2>/dev/null || true
        cp "$DEVICE_CONFIG/drastic.cf2" "$SHARED_USERDATA_PATH/NDS-advanced-drastic/config/" 2>/dev/null || true
    fi

    # Move any leftover cheats to centralized location
    if [ -d "$EMU_DIR/cheats" ] && ls -A "$EMU_DIR/cheats" 2>/dev/null | grep -q .; then
        cd "$EMU_DIR/cheats"
        mv * "$SDCARD_PATH/Cheats/NDS/" || true
    fi

    # Bind-mount external locations into drastic directory
    mount -o bind "$SHARED_USERDATA_PATH/NDS-advanced-drastic/config" "$EMU_DIR/config"
    mount -o bind "$SDCARD_PATH/Saves/NDS" "$EMU_DIR/backup"
    mount -o bind "$SDCARD_PATH/Cheats/NDS" "$EMU_DIR/cheats"
    mount -o bind "$SHARED_USERDATA_PATH/NDS-advanced-drastic" "$EMU_DIR/savestates"

    # Launch drastic
    cd "$EMU_DIR"
    export HOME="$EMU_DIR"
    "$EMU_DIR/drastic" "$*"
}

main "$@"
