// AXI-Lite backend for the hw_rast engine abstraction (rast_axi.v).  On the
// Ultra96 the register block is mmap'd from /dev/mem; built with
// HW_RAST_AXI_SIM the same register traffic drives a Verilated rast_axi, so
// the driver is tested against the wrapper before it ever touches the board.
#include "hw_rast.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#ifdef HW_RAST_AXI_SIM
#include "Vrast_axi.h"
#include "verilated.h"
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

// register word indices (rast_axi.v)
enum {
  I_ID = 0, I_CTRL = 1, I_STATUS = 2, I_X_DIM = 3, I_Y_DIM = 4,
  I_TEX_ADDR = 5, I_TEX_DATA = 6, I_FB_ADDR = 7, I_FB_DATA = 8, I_MODE = 9,
  I_V0X = 16, I_W0 = 22,
  I_DZDX = 25, I_DZDY = 27, I_Z_START = 29,
  I_SW_START = 31, I_DSWDX = 33, I_DSWDY = 35,
  I_TW_START = 37, I_DTWDX = 39, I_DTWDY = 41,
  I_IW_START = 43, I_DIWDX = 44, I_DIWDY = 45,
  I_CR_START = 46, I_CG_START = 49, I_CB_START = 52,
};
static const uint32_t RAST_ID = 0x52415354;
static const uint32_t CTRL_GO = 1, CTRL_CLEAR = 2, CTRL_SWAP = 4;
static const uint32_t ST_BUSY = 1, ST_CLEARING = 2;

struct hw_rast {
#ifdef HW_RAST_AXI_SIM
  Vrast_axi *tb;
#else
  int fd;
  volatile uint32_t *regs;
#endif
  int w, h;
  uint32_t tex_next;   // TEX_ADDR after the last texel write
};

#ifdef HW_RAST_AXI_SIM
static inline void tick(Vrast_axi *tb) {
  tb->s_axi_aclk = 0; tb->eval();
  tb->s_axi_aclk = 1; tb->eval();
}

static void wr(hw_rast *d, int idx, uint32_t v) {
  Vrast_axi *tb = d->tb;
  tb->s_axi_awaddr = idx * 4; tb->s_axi_wdata = v; tb->s_axi_wstrb = 0xf;
  tb->s_axi_awvalid = 1; tb->s_axi_wvalid = 1; tb->s_axi_bready = 1;
  bool acc;
  do {
    tb->eval(); acc = tb->s_axi_awready; tick(tb);
  } while(!acc);
  tb->s_axi_awvalid = 0; tb->s_axi_wvalid = 0;
  while(tb->s_axi_bvalid) {
    tick(tb);
  }
  tb->s_axi_bready = 0;
}

static uint32_t rd(hw_rast *d, int idx) {
  Vrast_axi *tb = d->tb;
  tb->s_axi_araddr = idx * 4; tb->s_axi_arvalid = 1; tb->s_axi_rready = 0;
  bool acc;
  do {
    tb->eval(); acc = tb->s_axi_arready; tick(tb);
  } while(!acc);
  tb->s_axi_arvalid = 0;
  uint32_t v = tb->s_axi_rdata;
  tb->s_axi_rready = 1; tick(tb); tb->s_axi_rready = 0;
  return v;
}
#else
static inline void wr(hw_rast *d, int idx, uint32_t v) { d->regs[idx] = v; }
static inline uint32_t rd(hw_rast *d, int idx) { return d->regs[idx]; }
#endif

static void wait_idle(hw_rast *d) {
  while(rd(d, I_STATUS) & (ST_BUSY | ST_CLEARING)) {
  }
}

static inline void wr64(hw_rast *d, int idx, int64_t v) {
  wr(d, idx, (uint32_t)v);
  wr(d, idx + 1, (uint32_t)((uint64_t)v >> 32));
}

extern "C" hw_rast *hw_open(int width, int height) {
  hw_rast *d = new hw_rast;
  d->w = width; d->h = height;
  d->tex_next = ~0u;
#ifdef HW_RAST_AXI_SIM
  d->tb = new Vrast_axi;
  Vrast_axi *tb = d->tb;
  tb->s_axi_aresetn = 0;
  tb->s_axi_awvalid = 0; tb->s_axi_wvalid = 0; tb->s_axi_bready = 0;
  tb->s_axi_arvalid = 0; tb->s_axi_rready = 0;
  tick(tb); tick(tb);
  tb->s_axi_aresetn = 1; tick(tb);
#else
  const char *base_s = getenv("HW_RAST_BASE");   // read once at open
  const off_t base = base_s ? strtoull(base_s, nullptr, 0) : 0xA0050000;
  d->fd = open("/dev/mem", O_RDWR | O_SYNC);
  if(d->fd < 0) {
    perror("open /dev/mem");
    exit(1);
  }
  void *p = mmap(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, d->fd, base);
  if(p == MAP_FAILED) {
    perror("mmap");
    exit(1);
  }
  d->regs = reinterpret_cast<volatile uint32_t*>(p);
#endif
  uint32_t id = rd(d, I_ID);
  if(id != RAST_ID) {
    fprintf(stderr, "hw_rast_axi: bad ID 0x%08x (expected 0x%08x)\n", id, RAST_ID);
    exit(1);
  }
  wr(d, I_X_DIM, width);
  wr(d, I_Y_DIM, height);
  wr(d, I_MODE, (1u << 16) | (255u << 8));
  return d;
}

extern "C" void hw_close(hw_rast *d) {
  wait_idle(d);
#ifdef HW_RAST_AXI_SIM
  delete d->tb;
#else
  munmap(const_cast<uint32_t*>(d->regs), 4096);
  close(d->fd);
#endif
  delete d;
}

extern "C" void hw_clear(hw_rast *d) {
  wait_idle(d);
  wr(d, I_CTRL, CTRL_CLEAR);
  wait_idle(d);
}

// TEX_ADDR auto-increments, so a run of consecutive texels in one bank costs
// one write each; only a jump (or bank change) rewrites the address.
extern "C" void hw_tex_write(hw_rast *d, int bank, uint32_t addr, uint32_t rgb) {
  uint32_t a = ((uint32_t)bank << 30) | addr;
  wait_idle(d);
  if(a != d->tex_next) {
    wr(d, I_TEX_ADDR, a);
  }
  wr(d, I_TEX_DATA, rgb);
  d->tex_next = a + 1;
}

// The engine reads the planes for the whole render, so wait for the previous
// triangle before rewriting them; return as soon as this one is started.
extern "C" void hw_draw_tri(hw_rast *d, const hw_tri *t) {
  wait_idle(d);
  wr(d, I_V0X + 0, t->v0x); wr(d, I_V0X + 1, t->v0y);
  wr(d, I_V0X + 2, t->v1x); wr(d, I_V0X + 3, t->v1y);
  wr(d, I_V0X + 4, t->v2x); wr(d, I_V0X + 5, t->v2y);
  wr(d, I_W0 + 0, t->w0); wr(d, I_W0 + 1, t->w1); wr(d, I_W0 + 2, t->w2);
  wr64(d, I_DZDX, t->dzdx); wr64(d, I_DZDY, t->dzdy); wr64(d, I_Z_START, t->z_start);
  wr64(d, I_SW_START, t->sw_start); wr64(d, I_DSWDX, t->dswdx); wr64(d, I_DSWDY, t->dswdy);
  wr64(d, I_TW_START, t->tw_start); wr64(d, I_DTWDX, t->dtwdx); wr64(d, I_DTWDY, t->dtwdy);
  wr(d, I_IW_START, t->iw_start); wr(d, I_DIWDX, t->diwdx); wr(d, I_DIWDY, t->diwdy);
  wr(d, I_CR_START + 0, t->cr_start); wr(d, I_CR_START + 1, t->dcrdx); wr(d, I_CR_START + 2, t->dcrdy);
  wr(d, I_CG_START + 0, t->cg_start); wr(d, I_CG_START + 1, t->dcgdx); wr(d, I_CG_START + 2, t->dcgdy);
  wr(d, I_CB_START + 0, t->cb_start); wr(d, I_CB_START + 1, t->dcbdx); wr(d, I_CB_START + 2, t->dcbdy);
  wr(d, I_MODE, (t->blend_mode & 3) | ((uint32_t)t->alpha << 8) | ((uint32_t)(t->z_enable & 1) << 16));
  wr(d, I_CTRL, CTRL_GO);
}

extern "C" void hw_swap(hw_rast *d) {
  wait_idle(d);
  wr(d, I_CTRL, CTRL_SWAP);
}

// Present the frame just drawn (swap) and scan it out of the front buffer;
// FB_DATA auto-increments.
extern "C" void hw_fb_read(hw_rast *d, uint8_t *rgb) {
  hw_swap(d);
  wr(d, I_FB_ADDR, 0);
  const int n = d->w * d->h;
  for(int a = 0; a < n; a++) {
    uint32_t c = rd(d, I_FB_DATA);
    rgb[3*a + 0] = (c >> 16) & 0xff;
    rgb[3*a + 1] = (c >> 8) & 0xff;
    rgb[3*a + 2] = c & 0xff;
  }
}
