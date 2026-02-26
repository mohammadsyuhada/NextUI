#!/usr/bin/env python3
"""Fix performance issues in the ADVDRASTIC_DRM rendering path.

Issues fixed:
1. Double gbm_surface_lock_front_buffer() in GFX_Flip() — first call locks a
   buffer BEFORE eglSwapBuffers, consuming a GBM buffer slot and causing stalls.
   The locked buffer pointer is then overwritten by the second call, leaking it.
"""
import sys


def fix_perf(content):
    changes = 0

    # Fix 1: Remove double gbm_surface_lock_front_buffer() in GFX_Flip
    # The first call (before eglSwapBuffers) locks the old front buffer,
    # wastes a GBM buffer slot, and the pointer is immediately overwritten.
    old_gfx_flip = '''void GFX_Flip(void)
{
#ifdef ADVDRASTIC_DRM
    struct gbm_bo *bo = gbm_surface_lock_front_buffer(drm_buf.gbm_surface);
    uint32_t handle = gbm_bo_get_handle(bo).u32;
    uint32_t pitch = gbm_bo_get_stride(bo);
    uint32_t fb;
#endif

    eglSwapBuffers(vid.eglDisplay, vid.eglSurface);

#ifdef ADVDRASTIC_DRM
    // GBM Buffer Swap
    bo = gbm_surface_lock_front_buffer(drm_buf.gbm_surface);
    handle = gbm_bo_get_handle(bo).u32;
    pitch = gbm_bo_get_stride(bo);'''

    new_gfx_flip = '''void GFX_Flip(void)
{
    eglSwapBuffers(vid.eglDisplay, vid.eglSurface);

#ifdef ADVDRASTIC_DRM
    {
    struct gbm_bo *bo;
    uint32_t handle, pitch, fb;

    // Lock the front buffer AFTER swap (not before)
    bo = gbm_surface_lock_front_buffer(drm_buf.gbm_surface);
    handle = gbm_bo_get_handle(bo).u32;
    pitch = gbm_bo_get_stride(bo);'''

    if old_gfx_flip in content:
        content = content.replace(old_gfx_flip, new_gfx_flip)
        # Close the extra brace we opened
        content = content.replace(
            '    drm_buf.fb_id = fb;\n    drm_buf.previous_bo = bo;\n#endif',
            '    drm_buf.fb_id = fb;\n    drm_buf.previous_bo = bo;\n    }\n#endif'
        )
        print("fix-perf: fixed double gbm_surface_lock_front_buffer in GFX_Flip")
        changes += 1
    else:
        print("fix-perf: WARNING - could not find GFX_Flip pattern")

    if changes == 0:
        print("fix-perf: no changes applied")
    else:
        print(f"fix-perf: applied {changes} fixes")

    return content


if __name__ == '__main__':
    filepath = sys.argv[1]
    with open(filepath, 'r') as f:
        content = f.read()
    content = fix_perf(content)
    with open(filepath, 'w') as f:
        f.write(content)
    print(f"fix-perf: wrote {filepath}")
