// TinyGL hardware backend.
//
// TinyGL keeps doing transform / lighting / clipping (including the near plane);
// it hands the low-level rasterizer final screen-space vertices (ZBufferPoint).
// We replace that software rasterizer (the ZB_* functions from ztriangle.c and
// zline.c) with our HW engine via gfx/hw_rast.  Link this together with TinyGL's
// objects MINUS ztriangle.o and zline.o, so these symbols take their place.
//
// Color-buffer clear and scan-out are driven by the harness directly (hw_clear /
// hw_fb_read); TinyGL's own ZBuffer (zb->pbuf/zbuf) is allocated but unused.

#include <cstdint>
extern "C" {
#include "GL/gl.h"
#include "zbuffer.h"
}
#include "gfx.h"
#include "hw_rast.h"

static hw_rast *g_dev = 0;
extern "C" void tgl_set_device(hw_rast *d) { g_dev = d; }

// TinyGL viewport depth (gl_eval_viewport): zp.z in [0, ZSIZE], near = large,
// far = 0.  Our engine depth is 24-bit, smaller = nearer, clear = max.  So
// invert and rescale 2^30 -> 2^24.
static const long    TGL_ZSIZE = 1L << (16 + 14);   // ZB_Z_BITS + ZB_POINT_Z_FRAC_BITS
static const int32_t OUR_ZMAX  = (1 << 24) - 1;

static inline int32_t map_depth(GLint z) {
  long d = (TGL_ZSIZE - (long)z) >> 6;
  if (d < 0) d = 0;
  if (d > OUR_ZMAX) d = OUR_ZMAX;
  return (int32_t)d;
}
// zp.r/g/b are ~v*0xfe0000 fixed point; the 8-bit color is the top byte.
static inline uint8_t col8(GLint c) { return (uint8_t)((c >> 16) & 0xff); }

static void submit(ZBuffer *zb, ZBufferPoint *a, ZBufferPoint *b, ZBufferPoint *c) {
  if (!g_dev) return;
  ZBufferPoint *vin[3] = {a, b, c};
  int32_t pos[3][3];
  float   uv[3][2] = {{0,0},{0,0},{0,0}};   // untextured (white texture bound)
  float   invw[3]  = {1.0f, 1.0f, 1.0f};
  uint8_t col[3][3];
  for (int i = 0; i < 3; i++) {
    pos[i][0] = vin[i]->x * 32;            // whole-pixel -> our sub-pixel (x32)
    pos[i][1] = vin[i]->y * 32;            // zp.y is already top-down
    pos[i][2] = map_depth(vin[i]->z);
    col[i][0] = col8(vin[i]->r);
    col[i][1] = col8(vin[i]->g);
    col[i][2] = col8(vin[i]->b);
  }
  // our coverage needs positive screen area; fix winding (swap v1,v2)
  long area = (long)(pos[1][0]-pos[0][0])*(pos[2][1]-pos[0][1])
            - (long)(pos[2][0]-pos[0][0])*(pos[1][1]-pos[0][1]);
  if (area == 0) return;
  if (area < 0) {
    for (int k = 0; k < 3; k++) { int32_t t = pos[1][k]; pos[1][k] = pos[2][k]; pos[2][k] = t; }
    for (int k = 0; k < 3; k++) { uint8_t t = col[1][k]; col[1][k] = col[2][k]; col[2][k] = t; }
  }
  uint8_t z_enable = zb->depth_test ? 1 : 0;
  submit_triangle(g_dev, pos, uv, invw, col, 0 /*opaque*/, 255, z_enable);
}

// 1px-ish line/point -> quad (2 triangles), like the IRIS GL backend.
static void line_quad(ZBuffer *zb, ZBufferPoint *v0, ZBufferPoint *v1) {
  ZBufferPoint q[4] = {*v0, *v0, *v1, *v1};
  int dx = v1->x - v0->x, dy = v1->y - v0->y;
  int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
  if (ax >= ay) { q[1].y += 1; q[2].y += 1; }      // mostly horizontal: thicken in y
  else          { q[1].x += 1; q[2].x += 1; }      // mostly vertical:   thicken in x
  submit(zb, &q[0], &q[1], &q[2]);
  submit(zb, &q[2], &q[3], &q[0]);
}

extern "C" {

void ZB_fillTriangleFlat        (ZBuffer *zb, ZBufferPoint *p0, ZBufferPoint *p1, ZBufferPoint *p2) { submit(zb,p0,p1,p2); }
void ZB_fillTriangleFlatNOBLEND (ZBuffer *zb, ZBufferPoint *p0, ZBufferPoint *p1, ZBufferPoint *p2) { submit(zb,p0,p1,p2); }
void ZB_fillTriangleSmooth      (ZBuffer *zb, ZBufferPoint *p0, ZBufferPoint *p1, ZBufferPoint *p2) { submit(zb,p0,p1,p2); }
void ZB_fillTriangleSmoothNOBLEND(ZBuffer *zb, ZBufferPoint *p0, ZBufferPoint *p1, ZBufferPoint *p2) { submit(zb,p0,p1,p2); }
// textured fills: routed untextured for now (gears is untextured).  TODO: upload
// the bound texture to the engine and carry perspective u/v + 1/w.
void ZB_fillTriangleMappingPerspective       (ZBuffer *zb, ZBufferPoint *p0, ZBufferPoint *p1, ZBufferPoint *p2) { submit(zb,p0,p1,p2); }
void ZB_fillTriangleMappingPerspectiveNOBLEND(ZBuffer *zb, ZBufferPoint *p0, ZBufferPoint *p1, ZBufferPoint *p2) { submit(zb,p0,p1,p2); }

void ZB_setTexture(ZBuffer *zb, PIXEL *texture) { zb->current_texture = texture; }

void ZB_line  (ZBuffer *zb, ZBufferPoint *p1, ZBufferPoint *p2) { line_quad(zb, p1, p2); }
void ZB_line_z(ZBuffer *zb, ZBufferPoint *p1, ZBufferPoint *p2) { line_quad(zb, p1, p2); }
void ZB_plot  (ZBuffer *zb, ZBufferPoint *p) {
  ZBufferPoint q[4] = {*p, *p, *p, *p};
  q[1].y += 1; q[2].x += 1; q[2].y += 1; q[3].x += 1;
  submit(zb, &q[0], &q[1], &q[2]);
  submit(zb, &q[2], &q[3], &q[0]);
}

} // extern "C"
