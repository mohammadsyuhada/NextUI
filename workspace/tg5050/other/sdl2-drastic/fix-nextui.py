#!/usr/bin/env python3
"""
fix-nextui.py — Apply NextUI-specific customizations to drastic_video.c

Changes applied:
  1. DRM page flip: async page flip with poll/event handler (smoother vsync)
  2. Menu: hide "Load new game", auto-skip hidden items in GUI input
  3. Menu: remove "Change Steward Options" item, fix item count/indices
  4. Menu: re-enable background images (bg0/bg1) and null-check them
  5. Menu: show version from drastic header instead of hardcoded text
  6. Hotkeys: layout cycling always uses normal layouts (hres_mode=0)
  7. Hotkeys: theme cycling allowed for all non-transparent layouts
  8. Rendering: disable automatic hres_mode switching in process_screen
  9. Rendering: don't break on hires — render both screens for all layouts
 10. GUI input: hook sdl_get_gui_input for proper d-pad/button handling
 11. Debug logging for hotkey combos and config map (for development)

Usage:
    python3 fix-nextui.py src/video/drastic_video.c

The companion fix-nextui.patch must be in the same directory as this script.
"""

import os
import subprocess
import sys

def main():
    if len(sys.argv) < 2:
        print("Usage: fix-nextui.py <path-to-drastic_video.c>")
        sys.exit(1)

    target = sys.argv[1]
    if not os.path.isfile(target):
        print(f"fix-nextui: ERROR: {target} not found")
        sys.exit(1)

    script_dir = os.path.dirname(os.path.abspath(__file__))
    patch_file = os.path.join(script_dir, "fix-nextui.patch")

    if not os.path.isfile(patch_file):
        print(f"fix-nextui: ERROR: {patch_file} not found")
        sys.exit(1)

    # Apply patch using the patch command
    # The patch uses paths like src/video/drastic_video.c, so run from the
    # SDL_drastic root directory (two levels up from the target file)
    sdl_root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(target))))
    result = subprocess.run(
        ["patch", "--forward", "--no-backup-if-mismatch", "-p0", "-i", patch_file],
        cwd=sdl_root,
        capture_output=True,
        text=True,
    )

    if result.returncode == 0:
        print(f"fix-nextui: applied patch to {target}")
    elif "already applied" in result.stdout.lower() or "reversed" in result.stdout.lower():
        print(f"fix-nextui: patch already applied to {target}")
    else:
        print(f"fix-nextui: WARNING: patch returned {result.returncode}")
        print(f"  stdout: {result.stdout}")
        print(f"  stderr: {result.stderr}")
        sys.exit(1)

if __name__ == "__main__":
    main()
