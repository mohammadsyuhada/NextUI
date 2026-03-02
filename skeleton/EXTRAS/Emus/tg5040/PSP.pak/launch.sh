#!/bin/sh
EMU_TAG=$(basename "$(dirname "$0")" .pak)
PAK_DIR="$(dirname "$0")"
EMU_DIR="$SDCARD_PATH/Emus/shared/ppsspp"
ROM="$1"

mkdir -p "$SAVES_PATH/$EMU_TAG"

# Single cluster: cpu0-3 (Cortex-A53, max 2000 MHz)
echo 1 >/sys/devices/system/cpu/cpu1/online 2>/dev/null
echo 1 >/sys/devices/system/cpu/cpu2/online 2>/dev/null
echo 1 >/sys/devices/system/cpu/cpu3/online 2>/dev/null
echo performance >/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
echo 2000000 >/sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq
echo 1608000 >/sys/devices/system/cpu/cpu0/cpufreq/scaling_min_freq

# Memory management: swap for large PSP games
SWAPFILE="/mnt/UDISK/psp_swap"
if [ ! -f "$SWAPFILE" ]; then
    dd if=/dev/zero of="$SWAPFILE" bs=1M count=256 2>/dev/null
    mkswap "$SWAPFILE" 2>/dev/null
fi
swapon "$SWAPFILE" 2>/dev/null
echo 200 >/proc/sys/vm/vfs_cache_pressure 2>/dev/null
sync
echo 3 >/proc/sys/vm/drop_caches 2>/dev/null

# User data directory (config, saves, cache)
USERDATA_DIR="$SHARED_USERDATA_PATH/PSP-ppsspp"
mkdir -p "$USERDATA_DIR/PSP/SYSTEM"
mkdir -p "$USERDATA_DIR/PSP/SAVEDATA"
mkdir -p "$USERDATA_DIR/PSP/PPSSPP_STATE"

# First run: copy device-specific defaults
if [ ! -f "$USERDATA_DIR/.tg5040_initialized" ]; then
    cp "$PAK_DIR/default.ini" "$USERDATA_DIR/PSP/SYSTEM/ppsspp.ini"
    touch "$USERDATA_DIR/.tg5040_initialized"
fi

export HOME="$USERDATA_DIR"
export LD_LIBRARY_PATH="$PAK_DIR:$EMU_DIR:$SDCARD_PATH/.system/tg5040/lib:/usr/trimui/lib:$LD_LIBRARY_PATH"
export LD_PRELOAD="libEGL.so"

# Overlay menu config
export EMU_OVERLAY_JSON="$EMU_DIR/overlay_settings.json"
export EMU_OVERLAY_INI="$USERDATA_DIR/PSP/SYSTEM/ppsspp.ini"
export EMU_OVERLAY_GAME="$(basename "$ROM" | sed 's/\.[^.]*$//')"
# Font and icon resources for overlay menu (from NextUI system resources)
FONT_FILE=$(ls "$SDCARD_PATH/.system/res/"*.ttf 2>/dev/null | head -1)
export EMU_OVERLAY_FONT="${FONT_FILE:-$SDCARD_PATH/.system/res/font.ttf}"
export EMU_OVERLAY_RES="$SDCARD_PATH/.system/res"
# Screenshot directory (matches minarch's .minui path for game switcher)
MINUI_DIR="$SHARED_USERDATA_PATH/.minui/$EMU_TAG"
mkdir -p "$MINUI_DIR"
export EMU_OVERLAY_SCREENSHOT_DIR="$MINUI_DIR"
export EMU_OVERLAY_ROMFILE="$(basename "$ROM")"

# Launch PPSSPP
cd "$EMU_DIR"
./PPSSPPSDL --fullscreen --xres 1024 --yres 768 "$ROM" &> "$LOGS_PATH/$EMU_TAG.txt" &
EMU_PID=$!
sleep 3

# Thread pinning (cpu0-3 are all Cortex-A53 @ 2000 MHz):
#   main thread (SDL + emu)    → cpu0
#   render thread (GL)         → cpu1
#   audio + helpers            → cpu2-3
taskset -p 1 "$EMU_PID" 2>/dev/null   # mask 0x1 = cpu0

# Pin known helper threads to cpu2-3
for TID in $(ls /proc/$EMU_PID/task/ 2>/dev/null); do
    [ "$TID" = "$EMU_PID" ] && continue
    TNAME=$(cat /proc/$EMU_PID/task/$TID/comm 2>/dev/null)
    case "$TNAME" in
        SDLAudioP2|SDLHotplug*|SDLTimer|mali-*)
            taskset -p 0xc "$TID" 2>/dev/null ;;  # mask 0xc = cpu2-3
    esac
done

# Find the busiest non-main thread (render/emu thread) and pin to cpu1
sleep 2
BEST_TID=""
BEST_UTIME=0
for TID in $(ls /proc/$EMU_PID/task/ 2>/dev/null); do
    [ "$TID" = "$EMU_PID" ] && continue
    TNAME=$(cat /proc/$EMU_PID/task/$TID/comm 2>/dev/null)
    case "$TNAME" in
        EmuThread|PPSSPPSDL)
            UTIME=$(awk '{print $14}' /proc/$EMU_PID/task/$TID/stat 2>/dev/null)
            UTIME=${UTIME:-0}
            if [ "$UTIME" -gt "$BEST_UTIME" ]; then
                BEST_UTIME=$UTIME
                BEST_TID=$TID
            fi
            ;;
    esac
done
[ -n "$BEST_TID" ] && taskset -p 2 "$BEST_TID" 2>/dev/null  # mask 0x2 = cpu1

wait $EMU_PID

# Cleanup: disable swap, restore VM defaults
swapoff "$SWAPFILE" 2>/dev/null
echo 100 >/proc/sys/vm/vfs_cache_pressure 2>/dev/null
