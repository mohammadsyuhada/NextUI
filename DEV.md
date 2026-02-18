# Desktop Development Setup

## Prerequisites

Install dependencies via Homebrew:

```bash
brew install gcc sdl2 sdl2_image sdl2_ttf sqlite libsamplerate
```

## One-time Setup

### 1. Create GCC symlinks

The build expects `gcc` to be Homebrew's GCC (not Apple Clang). This script symlinks Homebrew's GCC binaries into `/usr/local/bin/`:

```bash
sudo ./workspace/desktop/macos_create_gcc_symlinks.sh
```

Verify with:

```bash
gcc --version  # Should say "Homebrew GCC", not "Apple clang"
```

### 2. Prepare fake SD card root

The desktop build uses `/var/tmp/nextui/sdcard` as a stand-in for the device's SD card:

```bash
./workspace/desktop/prepare_fake_sd_root.sh
```

## Building

### Build libmsettings (required first)

```bash
cd workspace/desktop/libmsettings
make build CROSS_COMPILE=/usr/local/bin/ PREFIX=/opt/homebrew PREFIX_LOCAL=/opt/homebrew
```

### Build nextui

```bash
cd workspace/all/nextui
make PLATFORM=desktop CROSS_COMPILE=/usr/local/bin/ PREFIX=/opt/homebrew PREFIX_LOCAL=/opt/homebrew UNAME_S=Darwin
```

The binary is output to `workspace/all/nextui/build/desktop/nextui.elf`.

## Running

```bash
cd workspace/all/nextui
DYLD_LIBRARY_PATH=/opt/homebrew/lib ./build/desktop/nextui.elf
```

## IDE Setup (VS Code)

The project includes `.clangd` config for code intelligence. Install the **clangd** extension (`llvm-vs-code-extensions.vscode-clangd`).

Format-on-save is enabled via `.clang-format` + `.vscode/settings.json`.

## Code Formatting

The project uses `clang-format`. Install it:

```bash
brew install clang-format
```

Format all first-party files:

```bash
git ls-files -- 'workspace/**/*.c' 'workspace/**/*.h' | \
  grep -v 'minarch/libretro-common/' | \
  grep -v 'src/include/parson/' | \
  grep -v 'src/include/mbedtls' | \
  grep -v 'src/audio/kiss_fft' | \
  grep -v '_unmaintained/' | \
  xargs clang-format -i
```

Install the pre-commit hook to enforce formatting:

```bash
./scripts/install-hooks.sh
```
