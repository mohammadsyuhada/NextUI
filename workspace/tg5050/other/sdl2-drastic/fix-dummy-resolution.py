#!/usr/bin/env python3
"""Fix SDL2 dummy video driver to read display resolution from DRM.

The dummy driver hardcodes 1024x768. On tg5050 the actual display is 1280x720.
The hook library's GFX_Init() calls SDL_GetCurrentDisplayMode() which returns
the dummy driver's mode, causing resolution mismatch and truncated display.

This patch makes the dummy driver query DRM for the actual display resolution
at init time, falling back to 1024x768 if DRM is unavailable.
"""
import sys


def fix_dummy_resolution(content):
    # Add DRM headers after existing includes
    old_includes = '#include "SDL_hints.h"'
    new_includes = '''#include "SDL_hints.h"

#ifdef ADVDRASTIC_DRM
#include <fcntl.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#endif'''

    if old_includes in content:
        content = content.replace(old_includes, new_includes)
        print("fix-dummy-resolution: added DRM includes")
    else:
        print("fix-dummy-resolution: WARNING - could not find SDL_hints.h include")

    # Replace the hardcoded resolution with DRM query
    old_init = '''    /* Use a fake 32-bpp desktop mode */
    SDL_zero(mode);
    mode.format = SDL_PIXELFORMAT_RGB888;
    mode.w = 1024;
    mode.h = 768;
    mode.refresh_rate = 60;
    mode.driverdata = NULL;'''

    new_init = '''    /* Use a fake 32-bpp desktop mode */
    SDL_zero(mode);
    mode.format = SDL_PIXELFORMAT_RGB888;
    mode.w = 1024;
    mode.h = 768;
    mode.refresh_rate = 60;
    mode.driverdata = NULL;

#ifdef ADVDRASTIC_DRM
    /* Query actual display resolution from DRM */
    {
        int dri_fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
        if (dri_fd >= 0) {
            drmModeRes *res = drmModeGetResources(dri_fd);
            if (res) {
                int i;
                for (i = 0; i < res->count_connectors; i++) {
                    drmModeConnector *conn = drmModeGetConnector(dri_fd, res->connectors[i]);
                    if (conn && conn->connection == DRM_MODE_CONNECTED && conn->count_modes > 0) {
                        mode.w = conn->modes[0].hdisplay;
                        mode.h = conn->modes[0].vdisplay;
                        mode.refresh_rate = conn->modes[0].vrefresh;
                        printf("[SDL dummy] DRM display: %dx%d@%d\\n", mode.w, mode.h, mode.refresh_rate);
                        drmModeFreeConnector(conn);
                        break;
                    }
                    if (conn) drmModeFreeConnector(conn);
                }
                drmModeFreeResources(res);
            }
            close(dri_fd);
        }
    }
#endif'''

    if old_init in content:
        content = content.replace(old_init, new_init)
        print("fix-dummy-resolution: patched DUMMY_VideoInit with DRM resolution query")
    else:
        print("fix-dummy-resolution: WARNING - could not find DUMMY_VideoInit block")
        # Try line-level fix
        content = content.replace(
            'mode.w = 1024;\n    mode.h = 768;',
            'mode.w = 1280;\n    mode.h = 720;'
        )
        print("fix-dummy-resolution: applied fallback line-level fix (1280x720)")

    return content


if __name__ == '__main__':
    filepath = sys.argv[1]
    with open(filepath, 'r') as f:
        content = f.read()
    content = fix_dummy_resolution(content)
    with open(filepath, 'w') as f:
        f.write(content)
    print(f"fix-dummy-resolution: wrote {filepath}")
