# PPSSPP Standalone Emulator Build

Cross-compiled PPSSPP for TG5040/TG5050 with custom in-game overlay menu integration.

## Prerequisites

- Docker with TrimUI toolchain image:
  - `ghcr.io/loveretro/tg5040-toolchain:latest`

> **Note:** We use the tg5040 toolchain (GCC 8.3) for both platforms. The tg5050 toolchain
> (GCC 10.3) is missing GLES/KHR/EGL headers needed by PPSSPP.

## Source Setup

```bash
cd workspace/tg5040/other/ppsspp
git clone https://github.com/hrydgard/ppsspp.git ppsspp
cd ppsspp
git submodule update --init --recursive
```

## Source Patches

PPSSPP's source requires two patches for GCC 8.3 compatibility.

### 1. Screenshot.cpp — C++17 CTAD fix

GCC 8.3 doesn't support class template argument deduction (CTAD). In `Core/Screenshot.cpp`,
find `TakeGameScreenshot()` and patch the `IndependentTask` construction:

```diff
- g_threadManager.EnqueueTask(new IndependentTask(TaskType::IO_BLOCKING, TaskPriority::LOW,
-     [=]() {
+ auto task = [=]() {
          ...
-     }));
+ };
+ g_threadManager.EnqueueTask(new IndependentTask<decltype(task)>(TaskType::IO_BLOCKING, TaskPriority::LOW, std::move(task)));
```

There are two occurrences in the file (the `true` and `false` paths). Patch both.

### 2. SDLGLGraphicsContext.cpp — EGL globals

Make three EGL variables non-static so the overlay can access them for buffer swap:

```diff
- static EGLDisplay g_eglDisplay = EGL_NO_DISPLAY;
+ EGLDisplay g_eglDisplay = EGL_NO_DISPLAY;

- static EGLSurface g_eglSurface = nullptr;
+ EGLSurface g_eglSurface = nullptr;

- static bool useEGLSwap = false;
+ bool useEGLSwap = false;
```

## Build FFmpeg

PPSSPP bundles pre-built ffmpeg static libs, but they use GCC 10+ outline atomics
(`__aarch64_cas8_acq_rel`) which are incompatible with GCC 8.3. We rebuild ffmpeg
from source using our toolchain.

```bash
docker run --rm -v "$(pwd)/workspace":/root/workspace \
    ghcr.io/loveretro/tg5040-toolchain:latest bash -c \
    "cd /root/workspace/tg5040/other/ppsspp/ppsspp/ffmpeg && bash linux_nextui_arm64.sh"
```

This replaces `ffmpeg/linux/aarch64/lib/*.a` with GCC 8.3-compatible builds.

## Build PPSSPP (inside Docker toolchain)

```bash
docker run --rm -v "$(pwd)/workspace":/root/workspace \
    ghcr.io/loveretro/tg5040-toolchain:latest bash -c "
cd /root/workspace/tg5040/other/ppsspp/ppsspp
mkdir -p build && cd build
cmake -DCMAKE_TOOLCHAIN_FILE=../../toolchain-aarch64.cmake \
    -DUSING_GLES2=ON \
    -DUSING_EGL=ON \
    -DUSING_FBDEV=ON \
    -DUSE_WAYLAND_WSI=OFF \
    -DUSING_X11_VULKAN=OFF \
    -DVULKAN=OFF \
    -DUSE_DISCORD=OFF \
    -DUSE_MINIUPNPC=OFF \
    -DHEADLESS=OFF \
    -DUNITTEST=OFF \
    -DSIMULATOR=OFF \
    -DANDROID=OFF \
    -DUSE_SYSTEM_LIBPNG=OFF \
    -DUSE_FFMPEG=ON \
    -DCMAKE_BUILD_TYPE=Release \
    ..
make -j\$(nproc)
"
```

The binary will be at `build/PPSSPPSDL`.

## Deployment

Copy the following to the SD card:

```
Emus/shared/ppsspp/PPSSPPSDL          <- build/PPSSPPSDL
Emus/shared/ppsspp/assets/            <- ppsspp/assets/
Emus/shared/ppsspp/overlay_settings.json
```

Platform-specific files are already in the `PSP.pak` directories:

```
Emus/tg5040/PSP.pak/launch.sh
Emus/tg5040/PSP.pak/default.ini
Emus/tg5050/PSP.pak/launch.sh
Emus/tg5050/PSP.pak/default.ini
```

## Overlay Integration

The overlay hooks into `SDL/SDLMain.cpp` via `SDL/SDLOverlay.cpp`. When the menu button
(SDL button 8) is pressed during gameplay:

1. Emulation pauses via `Core_Break()`
2. Overlay menu renders using the shared `emu_overlay` library
3. Settings changes are written to `ppsspp.ini` and `g_Config.Reload()` is called
4. Save/load state uses PPSSPP's `SaveState::SaveSlot()`/`LoadSlot()`
5. Emulation resumes via `Core_Resume()`

## Build Notes

**macOS case-insensitive filesystem**: The overlay common dir (`workspace/all/common/`)
contains `sdl.h` which conflicts with `SDL.h` on macOS Docker volume mounts. The
CMakeLists.txt uses `set_source_files_properties()` to scope the overlay include path
to overlay source files only, and `SDLOverlay.h` uses `<SDL2/SDL.h>` instead of `"SDL.h"`.

**GLES3 gl3stub symbol conflict**: PPSSPP's `Common/GPU/OpenGL/gl3stub.c` redefines
GLES3 functions (`glBindVertexArray`, `glGenVertexArrays`, `glDeleteVertexArrays`, etc.)
as global function pointer variables loaded via `eglGetProcAddress`. These shadow the
`libGLESv2.so` symbols at link time, so any code calling e.g. `glBindVertexArray()`
actually calls through the gl3stub function pointer (which may be NULL if EGL init
failed). The overlay works around this by loading VAO functions directly via
`SDL_GL_GetProcAddress()` into its own `pfn_` prefixed function pointers in
`emu_overlay_sdl.c`.

**GL context management**: `SDLOverlay.cpp` captures the GL context during `Overlay_Init()`
(called after `InitFromRenderThread()` when the context is current) and calls
`SDL_GL_MakeCurrent()` before overlay GL operations to ensure the context is bound.

**SDL game controller event watcher bypass**: PPSSPP's `SDLJoystick` class registers an
`SDL_AddEventWatch` callback that processes `SDL_CONTROLLERBUTTONDOWN/UP` events *before*
they reach the SDL event queue. Our button 8 filter in `SDLMain.cpp` only catches raw
`SDL_JOYBUTTONDOWN/UP` events from the queue, so button 8's game controller event
(mapped to `SDL_CONTROLLER_BUTTON_BACK` via PPSSPP's default mapping in `setUpController()`)
bypasses the filter entirely. This causes PPSSPP's native pause menu to flash when the
overlay opens (the event watcher calls `NativeKey()` → `g_controlMapper.Key()` →
`pauseTrigger_` → `GamePauseScreen`). Fixed by adding `isOverlayMenuButton()` check in
`SDLJoystick::ProcessInput()` that uses `SDL_GameControllerGetBindForButton()` to detect
and skip any game controller button bound to raw button 8.

**GL framebuffer alpha in screenshots**: `glReadPixels` from the GLES framebuffer returns
alpha=0 for opaque game content (games don't write alpha). The overlay background renders
correctly because `draw_captured_frame` uses `SDL_BLENDMODE_NONE`. However, when the
captured frame is saved as a BMP screenshot and later loaded as an icon for the save/load
page, `draw_icon` uses `SDL_BLENDMODE_BLEND` — making alpha=0 pixels fully transparent.
Fixed by forcing alpha to 255 in `capture_frame()` in `emu_overlay_sdl.c`.

## Modified PPSSPP Files

- `SDL/SDLMain.cpp` — Added overlay include and menu button check in main loop; filter raw button 8 events
- `SDL/SDLJoystick.cpp` — Filter game controller events for button 8 (overlay menu) via `isOverlayMenuButton()`
- `SDL/SDLOverlay.cpp` — New file: overlay integration logic
- `SDL/SDLOverlay.h` — New file: overlay public API (uses `<SDL2/SDL.h>`)
- `SDL/SDLGLGraphicsContext.cpp` — Made EGL globals non-static for overlay buffer swap
- `CMakeLists.txt` — Added overlay source files with scoped include paths
- `Core/Screenshot.cpp` — Fixed CTAD for GCC 8.3 compatibility
- `ffmpeg/linux_nextui_arm64.sh` — New file: ffmpeg cross-build script for our toolchain
