// IRIS GL rasterizer backend: implements sgi-demos libgl's rasterizer.h by
// driving our hardware engine (gfx + hw_rast).  A drop-in replacement for
// libgl's reference_rasterizer.c -- gl.c hands us screen-space vertices and we
// rasterize them on the (Verilated, for now) PL.
//
// Conventions mirror reference_rasterizer.c: screen_vertex x,y are in
// 1/SCREEN_VERTEX_V2_SCALE pixels, GL y is bottom-up (we flip to top-down), z is
// 32-bit (we take the high 16), color is per-vertex (Gouraud).  Untextured, so
// we bind a white texture and the engine's texel*color modulate passes color
// through.

// basic_types.h does `#ifndef bool / typedef uint8_t bool` -- the guard misses
// C++'s bool keyword, so predefine bool as a macro (to itself) to skip it.
#define bool bool
extern "C" {
#include "rasterizer.h"
}
#undef bool
#include "gfx.h"
#include "hw_rast.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

// must match XMAXSCREEN+1 / YMAXSCREEN+1 in sgi-demos include/gl/gl.h
#define DISPLAY_W 800
#define DISPLAY_H 480
#define TEXDIM    128            // must match RTL TEX_W

static hw_rast *dev = 0;
static float    the_linewidth = 1.0f;
static int      zbuffer_on = 0;

// HW_DEBUG=1 dumps the first few draw calls / triangles (read env once)
static int hw_debug() { static int v = -1; if(v < 0) v = getenv("HW_DEBUG") ? 1 : 0; return v; }

// the front buffer sdl_framebuffer presents: BGRA, top-down
static unsigned char frontbuf[DISPLAY_H * DISPLAY_W * 4];
static uint8_t       rgbbuf[DISPLAY_H * DISPLAY_W * 3];   // engine scan-out staging

// --- one screen-space triangle -> the engine ---
static void submit_screen_tri(const screen_vertex *a, const screen_vertex *b, const screen_vertex *c) {
  const screen_vertex *vin[3] = {a, b, c};
  int32_t pos[3][3];
  float   uv[3][2] = {{0,0},{0,0},{0,0}};   // untextured (white texture)
  float   invw[3]  = {1.0f, 1.0f, 1.0f};    // screen-space already; recip is a no-op
  uint8_t col[3][3];
  for(int i = 0; i < 3; i++) {
    // keep full sub-pixel precision (screen_vertex x,y are already ×32)
    pos[i][0] = vin[i]->x;
    pos[i][1] = (DISPLAY_H - 1) * SCREEN_VERTEX_V2_SCALE - vin[i]->y;  // GL y-up -> top-down
    pos[i][2] = (int32_t)(vin[i]->z >> 8);      // 32-bit z -> 24-bit depth
    col[i][0] = vin[i]->r; col[i][1] = vin[i]->g; col[i][2] = vin[i]->b;
  }
  // our coverage needs positive screen area; fix winding (swap v1,v2)
  long area = (long)(pos[1][0]-pos[0][0])*(pos[2][1]-pos[0][1])
            - (long)(pos[2][0]-pos[0][0])*(pos[1][1]-pos[0][1]);
  if(area == 0) return;
  if(area < 0) {
    for(int k = 0; k < 3; k++) { int32_t t = pos[1][k]; pos[1][k] = pos[2][k]; pos[2][k] = t; }
    for(int k = 0; k < 3; k++) { uint8_t t = col[1][k]; col[1][k] = col[2][k]; col[2][k] = t; }
  }
  if(hw_debug()) {
    static int n = 0;
    if(n++ < 8) fprintf(stderr, "tri %d: (%d,%d,%d)c%d,%d,%d (%d,%d,%d) (%d,%d,%d)\n", n,
        pos[0][0],pos[0][1],pos[0][2], col[0][0],col[0][1],col[0][2],
        pos[1][0],pos[1][1],pos[1][2], pos[2][0],pos[2][1],pos[2][2]);
  }
  submit_triangle(dev, pos, uv, invw, col, 0, 255, (uint8_t)zbuffer_on);
}

static void offset_clamp(screen_vertex *v, float dx, float dy) {
  float x = v->x + dx * SCREEN_VERTEX_V2_SCALE;
  float y = v->y + dy * SCREEN_VERTEX_V2_SCALE;
  float xmax = (DISPLAY_W - 1) * SCREEN_VERTEX_V2_SCALE;
  float ymax = (DISPLAY_H - 1) * SCREEN_VERTEX_V2_SCALE;
  v->x = (uint16_t)(x < 0 ? 0 : (x > xmax ? xmax : x));
  v->y = (uint16_t)(y < 0 ? 0 : (y > ymax ? ymax : y));
}

// lines and points -> quads (2 triangles), like reference_rasterizer.c
static void draw_point(const screen_vertex *v0) {
  screen_vertex q[4] = {*v0, *v0, *v0, *v0};
  offset_clamp(&q[0], 0, 0); offset_clamp(&q[1], 0, 1);
  offset_clamp(&q[2], 1, 1); offset_clamp(&q[3], 1, 0);
  submit_screen_tri(&q[0], &q[1], &q[2]);
  submit_screen_tri(&q[2], &q[3], &q[0]);
}

static void draw_line(const screen_vertex *v0, const screen_vertex *v1) {
  float dx = (float)v1->x - v0->x, dy = (float)v1->y - v0->y;
  if(dx == 0 && dy == 0) { draw_point(v0); return; }
  screen_vertex q[4] = {*v0, *v0, *v1, *v1};
  float hw = the_linewidth * 0.5f;
  if(fabsf(dx) > fabsf(dy)) {
    offset_clamp(&q[0], 0, -hw); offset_clamp(&q[1], 0, hw);
    offset_clamp(&q[2], 0,  hw); offset_clamp(&q[3], 0, -hw);
  } else {
    offset_clamp(&q[0], -hw, 0); offset_clamp(&q[1], hw, 0);
    offset_clamp(&q[2],  hw, 0); offset_clamp(&q[3], -hw, 0);
  }
  submit_screen_tri(&q[0], &q[1], &q[2]);
  submit_screen_tri(&q[2], &q[3], &q[0]);
}

// ---- rasterizer.h API ----

int32_t rasterizer_winopen(char *title) {
  (void)title;
  dev = hw_open(DISPLAY_W, DISPLAY_H);
  static uint8_t white[TEXDIM * TEXDIM * 3];
  memset(white, 0xff, sizeof white);
  load_texture(dev, white, TEXDIM);
  hw_clear(dev);
  return 1;
}

void rasterizer_draw(uint32_t type, uint32_t count, screen_vertex *v) {
  if(hw_debug()) { static int n=0; if(n++<12) fprintf(stderr,"draw type=%u count=%u\n",type,count); }
  if(type == DRAW_TRIANGLES)
    for(uint32_t i = 0; i < count / 3; i++) submit_screen_tri(&v[3*i], &v[3*i+1], &v[3*i+2]);
  else if(type == DRAW_LINES)
    for(uint32_t i = 0; i < count / 2; i++) draw_line(&v[2*i], &v[2*i+1]);
  else if(type == DRAW_POINTS)
    for(uint32_t i = 0; i < count; i++) draw_point(&v[i]);
}

void rasterizer_swap() {
  hw_fb_read(dev, rgbbuf);                 // engine FB (RGB, top-down) -> staging
  for(int i = 0; i < DISPLAY_W * DISPLAY_H; i++) {
    frontbuf[i*4 + 0] = rgbbuf[i*3 + 2];   // B
    frontbuf[i*4 + 1] = rgbbuf[i*3 + 1];   // G
    frontbuf[i*4 + 2] = rgbbuf[i*3 + 0];   // R
    frontbuf[i*4 + 3] = 255;
  }

  // Headless capture/exit, for verifying demos without a display.
  // GEN_FRAME_PPM_FILES=1 dumps frameNNNN.ppm; HW_MAX_FRAMES=N exits after N.
  static int frame = 0;
  if(getenv("GEN_FRAME_PPM_FILES")) {
    char name[64];
    snprintf(name, sizeof name, "frame%04d.ppm", frame);
    FILE *fp = fopen(name, "wb");
    if(fp) {
      fprintf(fp, "P6\n%d %d\n255\n", DISPLAY_W, DISPLAY_H);
      fwrite(rgbbuf, 1, DISPLAY_W * DISPLAY_H * 3, fp);   // rgbbuf is RGB, top-down
      fclose(fp);
    }
  }
  frame++;
  const char *mx = getenv("HW_MAX_FRAMES");
  if(mx && frame >= atoi(mx)) exit(0);
}

unsigned char *rasterizer_frontbuffer() { return frontbuf; }

void rasterizer_clear(uint8_t r, uint8_t g, uint8_t b, short ci)   { (void)r;(void)g;(void)b;(void)ci; hw_clear(dev); }
void rasterizer_zclear(uint32_t z)                                 { (void)z; hw_clear(dev); }
void rasterizer_czclear(uint8_t r, uint8_t g, uint8_t b, short ci, uint32_t z) { (void)r;(void)g;(void)b;(void)ci;(void)z; hw_clear(dev); }
void rasterizer_zbuffer(int enable)    { zbuffer_on = enable; }
void rasterizer_linewidth(float w)     { the_linewidth = w; }
void rasterizer_rgbmode(int enable)    { (void)enable; }

// not yet wired to the HW engine (first cut): text glyphs, stipple, buffer copies
void rasterizer_bitmap(uint32_t w, uint32_t rb, uint32_t h, screen_vertex *sv, uint8_t *bits) { (void)w;(void)rb;(void)h;(void)sv;(void)bits; }
void rasterizer_alpha_blit(uint32_t w, uint32_t rb, uint32_t h, screen_vertex *sv, uint8_t *a, uint8_t r, uint8_t g, uint8_t b) { (void)w;(void)rb;(void)h;(void)sv;(void)a;(void)r;(void)g;(void)b; }
void rasterizer_setpattern(uint16_t pattern[16]) { (void)pattern; }
void rasterizer_pattern(int enable)              { (void)enable; }
void rasterizer_cbuffer_draw(int f, int b)       { (void)f;(void)b; }
void rasterizer_copy_front_to_back()             { }
void rasterizer_copy_back_to_front()             { }
