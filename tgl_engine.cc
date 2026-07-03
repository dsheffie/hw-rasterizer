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

// per-frame heartbeat stats (read+reset by the vid layer each GL_EndRendering)
extern "C" { long tgl_tris = 0; long tgl_culls = 0; long tgl_texloads = 0;
             long tgl_lm_updates = 0; /* glTexSubImage2D calls (dynamic lightmaps) */ }

// --- texture residency ---------------------------------------------------
// The engine has ONE texture memory; TinyGL/Quake bind hundreds of textures.
// We keep the engine's texture RAM holding whatever was used last and reload
// only on change.  Untextured fills use a solid-white texture so the engine's
// GL_MODULATE (texel*color) reduces to pure vertex color.
#define TEXDIM 128                         // must match RTL TEX_W
static const void *g_resident = 0;         // key of texture currently in engine RAM
static const void *const WHITE_KEY = (const void*)1;

static void ensure_white() {
  if (g_resident == WHITE_KEY) return;
  static uint8_t white[TEXDIM*TEXDIM*3];
  static bool init = false;
  if (!init) { for (size_t i = 0; i < sizeof(white); i++) white[i] = 0xff; init = true; }
  load_texture(g_dev, white, TEXDIM);
  g_resident = WHITE_KEY;
  tgl_texloads++;
}

// Downsample TinyGL's 256^2 PIXEL (0xAARRGGBB) pixmap to a 128^2 RGB base and
// upload it (load_texture builds the mip chain).  Reloads only on change.
static void ensure_texture(const PIXEL *pm) {
  if (g_resident == pm) return;
  static uint8_t rgb[TEXDIM*TEXDIM*3];
  const int SRC = TGL_FEATURE_TEXTURE_DIM;   // 256
  for (int y = 0; y < TEXDIM; y++)
    for (int x = 0; x < TEXDIM; x++) {
      unsigned r = 0, g = 0, b = 0;
      for (int dy = 0; dy < 2; dy++)
        for (int dx = 0; dx < 2; dx++) {
          PIXEL p = pm[(2*y+dy)*SRC + (2*x+dx)];
          r += (p >> 16) & 0xff; g += (p >> 8) & 0xff; b += p & 0xff;
        }
      int o = (y*TEXDIM + x)*3;
      rgb[o] = r >> 2; rgb[o+1] = g >> 2; rgb[o+2] = b >> 2;
    }
  load_texture(g_dev, rgb, TEXDIM);
  g_resident = pm;
  tgl_texloads++;
}

// Normalized texture coordinate from TinyGL's fixed-point zp.s / zp.t
// (clip.c: zp.s = texcoord * (S_MAX-S_MIN) + S_MIN).
static const float S_DEN = 1.0f / (float)(ZB_POINT_S_MAX - ZB_POINT_S_MIN);
static const float T_DEN = 1.0f / (float)(ZB_POINT_T_MAX - ZB_POINT_T_MIN);

// Map TinyGL's glBlendFunc(sfactor,dfactor) to an engine blend mode.  The
// lightmap pass is glBlendFunc(GL_ZERO, GL_ONE_MINUS_SRC_COLOR) over an
// un-inverted lightmap (see texture.c) -> a plain modulate (dst*src).
static uint8_t blend_mode_from(ZBuffer *zb) {
  GLenum s = zb->sfactor, d = zb->dfactor;
  if (s == GL_ZERO      && d == GL_ONE_MINUS_SRC_COLOR) return 3;  // modulate (lightmaps)
  if (s == GL_ONE       && d == GL_ONE)                 return 2;  // additive
  if (s == GL_SRC_ALPHA && d == GL_ONE_MINUS_SRC_ALPHA) return 1;  // src-over
  return 1;                                                        // default: src-over
}

static void submit(ZBuffer *zb, ZBufferPoint *a, ZBufferPoint *b, ZBufferPoint *c,
                   bool textured, uint8_t blend_mode, uint8_t alpha) {
  if (!g_dev) return;
  ZBufferPoint *vin[3] = {a, b, c};
  int32_t pos[3][3];
  float   uv[3][2];
  float   invw[3];
  uint8_t col[3][3];

  if (textured) {
    // zp.z (TinyGL screen depth, near=large) is affine in 1/w, so it is the
    // perspective denominator: u = interp(uv*z)/interp(z) reproduces TinyGL's
    // own ss = sz/z exactly.  Scale zp.z (range [0, TGL_ZSIZE=2^30]) by a single
    // GLOBAL constant into iw in [0, 2^18], which in the engine's Q12 fixed point
    // (iw_start is 18.12, int32) exactly fills the range without overflow.
    //
    // The scale must be GLOBAL (same for every triangle), NOT per-triangle: with
    // a per-triangle normalization, two triangles sharing a floor edge scale that
    // edge's iw by different factors, so Q12 quantizes it differently on each side
    // -> a visible seam/kink where the shared perspective mapping should be
    // continuous (worst on large grazing floor polys).  A global scale also never
    // divides by a per-triangle extreme, so the far vertex's iw never collapses to
    // ~0 (which, with tiny lightmap UVs, underflowed uv*iw to 0 in Q12 -> garbage
    // luxels -> hard black triangles -- the failure mode of the old zmax scheme).
    const float GSCALE = (float)(1 << 18) / (float)TGL_ZSIZE;   // = 1/4096
    for (int i = 0; i < 3; i++) {
      uv[i][0] = (float)(vin[i]->s - ZB_POINT_S_MIN) * S_DEN;
      uv[i][1] = (float)(vin[i]->t - ZB_POINT_T_MIN) * T_DEN;
      invw[i] = (float)vin[i]->z * GSCALE;
    }
  } else {
    for (int i = 0; i < 3; i++) { uv[i][0] = uv[i][1] = 0.0f; invw[i] = 1.0f; }
  }

  for (int i = 0; i < 3; i++) {
    pos[i][0] = vin[i]->x * 32;            // whole-pixel -> our sub-pixel (x32)
    pos[i][1] = vin[i]->y * 32;            // zp.y is already top-down
    pos[i][2] = map_depth(vin[i]->z);
    // modulate (lightmap) pass is texture-only: force white so the engine's
    // texel*color reduces to the lightmap texel, then blend dst*lightmap.
    col[i][0] = blend_mode == 3 ? 255 : col8(vin[i]->r);
    col[i][1] = blend_mode == 3 ? 255 : col8(vin[i]->g);
    col[i][2] = blend_mode == 3 ? 255 : col8(vin[i]->b);
  }
  // our coverage needs positive screen area; fix winding (swap v1,v2)
  long area = (long)(pos[1][0]-pos[0][0])*(pos[2][1]-pos[0][1])
            - (long)(pos[2][0]-pos[0][0])*(pos[1][1]-pos[0][1]);
  if (area == 0) { tgl_culls++; return; }
  if (area < 0) {
    for (int k = 0; k < 3; k++) { int32_t t = pos[1][k]; pos[1][k] = pos[2][k]; pos[2][k] = t; }
    for (int k = 0; k < 3; k++) { uint8_t t = col[1][k]; col[1][k] = col[2][k]; col[2][k] = t; }
    for (int k = 0; k < 2; k++) { float t = uv[1][k]; uv[1][k] = uv[2][k]; uv[2][k] = t; }
    { float t = invw[1]; invw[1] = invw[2]; invw[2] = t; }
  }
  uint8_t z_enable = zb->depth_test ? 1 : 0;
  tgl_tris++;
  submit_triangle(g_dev, pos, uv, invw, col, blend_mode, alpha, z_enable);
}

// 1px-ish line/point -> quad (2 triangles), like the IRIS GL backend.
static void line_quad(ZBuffer *zb, ZBufferPoint *v0, ZBufferPoint *v1) {
  ZBufferPoint q[4] = {*v0, *v0, *v1, *v1};
  int dx = v1->x - v0->x, dy = v1->y - v0->y;
  int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
  if (ax >= ay) { q[1].y += 1; q[2].y += 1; }      // mostly horizontal: thicken in y
  else          { q[1].x += 1; q[2].x += 1; }      // mostly vertical:   thicken in x
  submit(zb, &q[0], &q[1], &q[2], false, 0, 255);
  submit(zb, &q[2], &q[3], &q[0], false, 0, 255);
}

// textured fill: make the bound texture resident, then submit with uv + 1/w.
// 'blend' = the BLEND fill variant (GL_BLEND on); pick the engine mode from the
// active glBlendFunc (the lightmap pass -> modulate).
static void submit_tex(ZBuffer *zb, ZBufferPoint *p0, ZBufferPoint *p1, ZBufferPoint *p2, bool blend) {
  uint8_t mode  = blend ? blend_mode_from(zb) : 0;
  uint8_t alpha = blend ? (uint8_t)(p0->a & 0xff) : 255;   // src-over (mode 1) only; other modes ignore it
  if (zb->current_texture) { ensure_texture(zb->current_texture); submit(zb, p0, p1, p2, true,  mode, alpha); }
  else                     { ensure_white();                      submit(zb, p0, p1, p2, false, mode, alpha); }
}

extern "C" {

// Untextured fills.  The non-NOBLEND variants are only reached with GL_BLEND on
// (clip.c gl_draw_triangle_fill), so map the active glBlendFunc to an engine mode
// and carry the source alpha (Quake's screen polyblend = src-over; flashblend
// dlight coronas = additive).  NOBLEND stays opaque.
static void submit_flat(ZBuffer *zb, ZBufferPoint *p0, ZBufferPoint *p1, ZBufferPoint *p2, bool blend) {
  ensure_white();
  uint8_t mode  = blend ? blend_mode_from(zb) : 0;
  uint8_t alpha = blend ? (uint8_t)(p0->a & 0xff) : 255;
  submit(zb, p0, p1, p2, false, mode, alpha);
}
void ZB_fillTriangleFlat        (ZBuffer *zb, ZBufferPoint *p0, ZBufferPoint *p1, ZBufferPoint *p2) { submit_flat(zb,p0,p1,p2,true);  }
void ZB_fillTriangleFlatNOBLEND (ZBuffer *zb, ZBufferPoint *p0, ZBufferPoint *p1, ZBufferPoint *p2) { submit_flat(zb,p0,p1,p2,false); }
void ZB_fillTriangleSmooth      (ZBuffer *zb, ZBufferPoint *p0, ZBufferPoint *p1, ZBufferPoint *p2) { submit_flat(zb,p0,p1,p2,true);  }
void ZB_fillTriangleSmoothNOBLEND(ZBuffer *zb, ZBufferPoint *p0, ZBufferPoint *p1, ZBufferPoint *p2) { submit_flat(zb,p0,p1,p2,false); }
// textured fills: upload the bound texture and carry perspective u/v + 1/w.
void ZB_fillTriangleMappingPerspective       (ZBuffer *zb, ZBufferPoint *p0, ZBufferPoint *p1, ZBufferPoint *p2) { submit_tex(zb,p0,p1,p2,true);  }
void ZB_fillTriangleMappingPerspectiveNOBLEND(ZBuffer *zb, ZBufferPoint *p0, ZBufferPoint *p1, ZBufferPoint *p2) { submit_tex(zb,p0,p1,p2,false); }

void ZB_setTexture(ZBuffer *zb, PIXEL *texture) { zb->current_texture = texture; }

void ZB_line  (ZBuffer *zb, ZBufferPoint *p1, ZBufferPoint *p2) { ensure_white(); line_quad(zb, p1, p2); }
void ZB_line_z(ZBuffer *zb, ZBufferPoint *p1, ZBufferPoint *p2) { ensure_white(); line_quad(zb, p1, p2); }
void ZB_plot  (ZBuffer *zb, ZBufferPoint *p) {
  ensure_white();
  ZBufferPoint q[4] = {*p, *p, *p, *p};
  q[1].y += 1; q[2].x += 1; q[2].y += 1; q[3].x += 1;
  submit(zb, &q[0], &q[1], &q[2], false, 0, 255);
  submit(zb, &q[2], &q[3], &q[0], false, 0, 255);
}

} // extern "C"
