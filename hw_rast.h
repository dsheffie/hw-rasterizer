#ifndef HW_RAST_H
#define HW_RAST_H

// Abstraction over the rasterizer engine.  The same API is implemented by the
// Verilated model (hw_rast_verilated.cc) now and an AXI/FPGA backend later; the
// graphics stack is built on this, not on the engine's transport directly.
//
// The host does the per-triangle setup math (edge functions, depth/attribute/
// color planes) and hands the engine a fully-set-up triangle; the backend just
// loads it and runs.  Field widths mirror the RTL ports; the backend masks/packs
// them for its transport.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  int32_t v0x, v0y, v1x, v1y, v2x, v2y;   // screen-space vertices
  int32_t w0, w1, w2;                     // edge weights at the bbox start corner
  int64_t dzdx, dzdy, z_start;            // depth plane
  int64_t sw_start, dswdx, dswdy;         // u/w plane
  int64_t tw_start, dtwdx, dtwdy;         // t/w plane
  int32_t iw_start, diwdx, diwdy;         // 1/w plane
  int32_t cr_start, dcrdx, dcrdy;         // Gouraud R plane
  int32_t cg_start, dcgdx, dcgdy;         // Gouraud G plane
  int32_t cb_start, dcbdx, dcbdy;         // Gouraud B plane
  uint8_t blend_mode;                     // 0 opaque, 1 src-over, 2 additive
  uint8_t alpha;                          // source alpha for src-over
} hw_tri;

typedef struct hw_rast hw_rast;           // opaque engine handle

hw_rast *hw_open(int width, int height);  // create + reset, set dimensions
void     hw_close(hw_rast *d);
void     hw_clear(hw_rast *d);                                   // clear color + depth
void     hw_tex_write(hw_rast *d, int bank, uint32_t addr, uint32_t rgb); // load one texel
void     hw_draw_tri(hw_rast *d, const hw_tri *t);              // load setup, run to done
void     hw_fb_read(hw_rast *d, uint8_t *rgb);                   // scan out width*height*3

#ifdef __cplusplus
}
#endif

#endif
