// Verilated-model backend for the hw_rast engine abstraction.  Drives the
// Vrasterize model by poking ports and ticking the clock.  A future FPGA
// backend implements the same hw_rast.h API over AXI instead.
#include "hw_rast.h"
#include "Vrasterize.h"
#include "verilated.h"

struct hw_rast {
  Vrasterize *tb;
  int w, h;
};

static inline void tick(Vrasterize *tb) {
  tb->clk = (~tb->clk) & 1; tb->eval();
  tb->clk = (~tb->clk) & 1; tb->eval();
}

extern "C" hw_rast *hw_open(int width, int height) {
  hw_rast *d = new hw_rast;
  d->tb = new Vrasterize;
  d->w = width; d->h = height;
  Vrasterize *tb = d->tb;
  tb->clk = 0; tb->rst = 1; tb->go = 0; tb->clear = 0;
  tb->tex_we = 0; tb->tex_wbank = 0; tb->blend_mode = 0; tb->tri_alpha = 255; tb->z_enable = 1;
  tb->fb_raddr = 0; tb->x_dim = width; tb->y_dim = height;
  tick(tb); tb->rst = 0; tick(tb);
  return d;
}

extern "C" void hw_close(hw_rast *d) { delete d->tb; delete d; }

extern "C" void hw_clear(hw_rast *d) {
  d->tb->clear = 1; tick(d->tb); d->tb->clear = 0;
  while(d->tb->clearing) tick(d->tb);
}

extern "C" void hw_tex_write(hw_rast *d, int bank, uint32_t addr, uint32_t rgb) {
  d->tb->tex_wbank = bank; d->tb->tex_waddr = addr; d->tb->tex_wdata = rgb;
  d->tb->tex_we = 1; tick(d->tb); d->tb->tex_we = 0;
}

extern "C" void hw_draw_tri(hw_rast *d, const hw_tri *t) {
  Vrasterize *tb = d->tb;
  const uint64_t SW_MASK = (1ULL << 48) - 1;   // u/w, t/w 36.12
  const uint32_t IW_MASK = (1u   << 30) - 1;   // 1/w 18.12
  const uint32_t COL_MASK = (1u  << 24) - 1;   // color planes 24-bit
  tb->v0_x = t->v0x; tb->v0_y = t->v0y;
  tb->v1_x = t->v1x; tb->v1_y = t->v1y;
  tb->v2_x = t->v2x; tb->v2_y = t->v2y;
  tb->w0 = t->w0; tb->w1 = t->w1; tb->w2 = t->w2;
  tb->dzdx = t->dzdx; tb->dzdy = t->dzdy; tb->z_start = t->z_start;
  tb->sw_start = (uint64_t)t->sw_start & SW_MASK;
  tb->dswdx    = (uint64_t)t->dswdx    & SW_MASK;
  tb->dswdy    = (uint64_t)t->dswdy    & SW_MASK;
  tb->tw_start = (uint64_t)t->tw_start & SW_MASK;
  tb->dtwdx    = (uint64_t)t->dtwdx    & SW_MASK;
  tb->dtwdy    = (uint64_t)t->dtwdy    & SW_MASK;
  tb->iw_start = (uint32_t)t->iw_start & IW_MASK;
  tb->diwdx    = (uint32_t)t->diwdx    & IW_MASK;
  tb->diwdy    = (uint32_t)t->diwdy    & IW_MASK;
  tb->cr_start = (uint32_t)t->cr_start & COL_MASK;
  tb->dcrdx    = (uint32_t)t->dcrdx    & COL_MASK;
  tb->dcrdy    = (uint32_t)t->dcrdy    & COL_MASK;
  tb->cg_start = (uint32_t)t->cg_start & COL_MASK;
  tb->dcgdx    = (uint32_t)t->dcgdx    & COL_MASK;
  tb->dcgdy    = (uint32_t)t->dcgdy    & COL_MASK;
  tb->cb_start = (uint32_t)t->cb_start & COL_MASK;
  tb->dcbdx    = (uint32_t)t->dcbdx    & COL_MASK;
  tb->dcbdy    = (uint32_t)t->dcbdy    & COL_MASK;
  tb->blend_mode = t->blend_mode; tb->tri_alpha = t->alpha; tb->z_enable = t->z_enable;
  tb->go = 1; tick(tb); tb->go = 0;
  while(!tb->done) tick(tb);
}

extern "C" void hw_fb_read(hw_rast *d, uint8_t *rgb) {
  const int n = d->w * d->h;
  for(int a = 0; a < n; a++) {
    d->tb->fb_raddr = a; tick(d->tb);
    uint32_t c = d->tb->fb_rdata;
    rgb[3*a + 0] = (c >> 16) & 0xff;
    rgb[3*a + 1] = (c >> 8) & 0xff;
    rgb[3*a + 2] = c & 0xff;
  }
}
