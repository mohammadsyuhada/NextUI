#!/usr/bin/env python3
"""Fix drm_init() in drastic_video.c for tg5050 compatibility.

Issues in the original code:
1. conn_id = res->connectors[1] — hardcoded to second connector (may not exist on tg5050)
2. gbm_surface_create(..., 640, 480, ...) — hardcoded resolution instead of actual display size
"""
import sys
import re

def fix_drm_init(content):
    # Replace the hardcoded drm_init() with auto-detecting version
    old_drm_init = '''int drm_init()
{
\t// Open Dri
\tdri_fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
\tif(dri_fd < 0){
\t\tprintf("dri wrong\\n");
\t\treturn -1;
\t}
        printf("fd flags: 0x%x\\n", fcntl(dri_fd, F_GETFL));

\tres = drmModeGetResources(dri_fd);
\tcrtc_id = res->crtcs[0];
\tconn_id = res->connectors[1];
\tprintf("crtc = %d , conneter = %d\\n",crtc_id,conn_id);

\tconn = drmModeGetConnector(dri_fd, conn_id);
\tdrm_buf.width = conn->modes[0].hdisplay;
\tdrm_buf.height = conn->modes[0].vdisplay;

\tprintf("width = %d , height = %d\\n",drm_buf.width,drm_buf.height);

\tdrm_create_fb(&drm_buf);
\t
\t// Init GBM Device
\tdrm_buf.gbm = gbm_create_device(dri_fd);
\tif (!drm_buf.gbm) {
\t\tfprintf(stderr, "Cannot create gbm device\\n");
\t\treturn -1;
\t}
\tdrm_buf.gbm_surface = gbm_surface_create(
\t\tdrm_buf.gbm,
\t\t640,
\t\t480,
\t\tGBM_FORMAT_ARGB8888,
\t\tGBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING
\t);

\tif (!drm_buf.gbm_surface) {
\t\tfprintf(stderr, "Cannot create gbm surface\\n");
\t\treturn -1;
\t}
\t//Set CRTCS
\tdrmModeSetCrtc(dri_fd, crtc_id, drm_buf.fb_id,
\t\t\t0, 0, &conn_id, 1, &conn->modes[0]);

\treturn 0;
}'''

    new_drm_init = '''int drm_init()
{
\t// Open DRI
\tdri_fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
\tif (dri_fd < 0) {
\t\tprintf("drm_init: failed to open /dev/dri/card0\\n");
\t\treturn -1;
\t}
\tprintf("drm_init: fd=%d flags=0x%x\\n", dri_fd, fcntl(dri_fd, F_GETFL));

\tres = drmModeGetResources(dri_fd);
\tif (!res) {
\t\tprintf("drm_init: drmModeGetResources failed\\n");
\t\treturn -1;
\t}
\tprintf("drm_init: connectors=%d, crtcs=%d\\n", res->count_connectors, res->count_crtcs);

\t// Auto-detect connected connector
\tconn_id = 0;
\tcrtc_id = 0;
\tfor (int i = 0; i < res->count_connectors; i++) {
\t\tdrmModeConnector *c = drmModeGetConnector(dri_fd, res->connectors[i]);
\t\tif (c && c->connection == DRM_MODE_CONNECTED && c->count_modes > 0) {
\t\t\tconn_id = c->connector_id;
\t\t\tprintf("drm_init: found connected connector %d (type=%d)\\n", conn_id, c->connector_type);
\t\t\tif (c->encoder_id) {
\t\t\t\tdrmModeEncoder *enc = drmModeGetEncoder(dri_fd, c->encoder_id);
\t\t\t\tif (enc) {
\t\t\t\t\tcrtc_id = enc->crtc_id;
\t\t\t\t\tdrmModeFreeEncoder(enc);
\t\t\t\t}
\t\t\t}
\t\t\tdrmModeFreeConnector(c);
\t\t\tbreak;
\t\t}
\t\tif (c) drmModeFreeConnector(c);
\t}
\tif (!conn_id) {
\t\tconn_id = res->connectors[0];
\t\tprintf("drm_init: no connected connector found, using first: %d\\n", conn_id);
\t}
\tif (!crtc_id && res->count_crtcs > 0) {
\t\tcrtc_id = res->crtcs[0];
\t}
\tprintf("drm_init: crtc=%d, connector=%d\\n", crtc_id, conn_id);

\tconn = drmModeGetConnector(dri_fd, conn_id);
\tif (!conn) {
\t\tprintf("drm_init: drmModeGetConnector failed\\n");
\t\treturn -1;
\t}
\tdrm_buf.width = conn->modes[0].hdisplay;
\tdrm_buf.height = conn->modes[0].vdisplay;
\tprintf("drm_init: display %dx%d\\n", drm_buf.width, drm_buf.height);

\tdrm_create_fb(&drm_buf);

\t// Init GBM Device
\tdrm_buf.gbm = gbm_create_device(dri_fd);
\tif (!drm_buf.gbm) {
\t\tfprintf(stderr, "drm_init: cannot create GBM device\\n");
\t\treturn -1;
\t}
\tdrm_buf.gbm_surface = gbm_surface_create(
\t\tdrm_buf.gbm,
\t\tdrm_buf.width,
\t\tdrm_buf.height,
\t\tGBM_FORMAT_ARGB8888,
\t\tGBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING
\t);
\tif (!drm_buf.gbm_surface) {
\t\tfprintf(stderr, "drm_init: cannot create GBM surface (%dx%d)\\n", drm_buf.width, drm_buf.height);
\t\treturn -1;
\t}
\tprintf("drm_init: GBM surface %dx%d created\\n", drm_buf.width, drm_buf.height);

\t// Set CRTC
\tint ret = drmModeSetCrtc(dri_fd, crtc_id, drm_buf.fb_id,
\t\t\t0, 0, &conn_id, 1, &conn->modes[0]);
\tif (ret < 0) {
\t\tprintf("drm_init: drmModeSetCrtc failed: %d\\n", ret);
\t}

\treturn 0;
}'''

    # Try exact match first
    if old_drm_init in content:
        content = content.replace(old_drm_init, new_drm_init)
        print("fix-drm-init: replaced drm_init() [exact match]")
    else:
        # Fuzzy match — find the function and replace it
        # Match from "int drm_init()" to the closing brace
        pattern = r'int drm_init\(\)\s*\{[^}]*?//Set CRTCS[^}]*?\}'
        match = re.search(pattern, content, re.DOTALL)
        if match:
            content = content[:match.start()] + new_drm_init + content[match.end():]
            print("fix-drm-init: replaced drm_init() [regex match]")
        else:
            # Last resort: line-by-line fixes
            print("fix-drm-init: WARNING - could not find drm_init() block, applying line fixes")
            # Fix connector index
            content = content.replace(
                'conn_id = res->connectors[1];',
                'conn_id = res->connectors[0]; // fixed: was [1]'
            )
            # Fix GBM surface size — match the 640/480 lines near gbm_surface_create
            lines = content.split('\n')
            in_gbm_create = False
            for i, line in enumerate(lines):
                if 'gbm_surface_create' in line:
                    in_gbm_create = True
                    continue
                if in_gbm_create:
                    if '640' in line:
                        lines[i] = line.replace('640', 'drm_buf.width')
                    if '480' in line:
                        lines[i] = line.replace('480', 'drm_buf.height')
                    if 'GBM_FORMAT' in line:
                        in_gbm_create = False
            content = '\n'.join(lines)
            print("fix-drm-init: applied line-level fixes")

    return content


if __name__ == '__main__':
    filepath = sys.argv[1]
    with open(filepath, 'r') as f:
        content = f.read()
    content = fix_drm_init(content)
    with open(filepath, 'w') as f:
        f.write(content)
    print(f"fix-drm-init: wrote {filepath}")
