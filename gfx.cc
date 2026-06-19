#include "gfx.h"
#include "setup.h"
#include <cmath>

// Set up one screen-space triangle and submit it.  All the per-triangle math
// (edge functions via setup_triangle, the u/w,t/w,1/w and color planes via
// setup_attr) happens here; the engine just iterates the result.
extern "C" void submit_triangle(hw_rast *d,
                                const int32_t pos[3][3],
                                const float   uv[3][2],
                                const float   invw[3],
                                const uint8_t col[3][3],
                                uint8_t blend_mode, uint8_t alpha) {
  vertex3d v0{pos[0][0], pos[0][1], pos[0][2]};
  vertex3d v1{pos[1][0], pos[1][1], pos[1][2]};
  vertex3d v2{pos[2][0], pos[2][1], pos[2][2]};

  tri_setup s = setup_triangle(v0, v1, v2);
  attr_plane Psw = setup_attr(v0, v1, v2, uv[0][0]*invw[0], uv[1][0]*invw[1], uv[2][0]*invw[2]);
  attr_plane Ptw = setup_attr(v0, v1, v2, uv[0][1]*invw[0], uv[1][1]*invw[1], uv[2][1]*invw[2]);
  attr_plane Piw = setup_attr(v0, v1, v2, invw[0], invw[1], invw[2]);
  attr_plane Pcr = setup_attr(v0, v1, v2, col[0][0], col[1][0], col[2][0]);
  attr_plane Pcg = setup_attr(v0, v1, v2, col[0][1], col[1][1], col[2][1]);
  attr_plane Pcb = setup_attr(v0, v1, v2, col[0][2], col[1][2], col[2][2]);

  const double S = (double)(1 << 12);   // ATTR_FRAC_BITS / COL_FRAC = 12
  hw_tri t;
  t.v0x = v0.x; t.v0y = v0.y; t.v1x = v1.x; t.v1y = v1.y; t.v2x = v2.x; t.v2y = v2.y;
  t.w0 = s.w0; t.w1 = s.w1; t.w2 = s.w2;
  t.dzdx = s.dzdx; t.dzdy = s.dzdy; t.z_start = s.z_start;
  t.sw_start = std::llround(Psw.a_start*S); t.dswdx = std::llround(Psw.dadx*S); t.dswdy = std::llround(Psw.dady*S);
  t.tw_start = std::llround(Ptw.a_start*S); t.dtwdx = std::llround(Ptw.dadx*S); t.dtwdy = std::llround(Ptw.dady*S);
  t.iw_start = (int32_t)std::llround(Piw.a_start*S); t.diwdx = (int32_t)std::llround(Piw.dadx*S); t.diwdy = (int32_t)std::llround(Piw.dady*S);
  t.cr_start = (int32_t)std::llround(Pcr.a_start*S); t.dcrdx = (int32_t)std::llround(Pcr.dadx*S); t.dcrdy = (int32_t)std::llround(Pcr.dady*S);
  t.cg_start = (int32_t)std::llround(Pcg.a_start*S); t.dcgdx = (int32_t)std::llround(Pcg.dadx*S); t.dcgdy = (int32_t)std::llround(Pcg.dady*S);
  t.cb_start = (int32_t)std::llround(Pcb.a_start*S); t.dcbdx = (int32_t)std::llround(Pcb.dadx*S); t.dcbdy = (int32_t)std::llround(Pcb.dady*S);
  t.blend_mode = blend_mode; t.alpha = alpha;
  hw_draw_tri(d, &t);
}

// Build the box-filter mip chain from a dim x dim RGB image and upload each
// level to bank {y&1,x&1} at the uniform (dim/2)x(dim/2) slot the RTL expects:
// addr = (level<<12) | ((y>>1)<<6) | (x>>1).  dim must be the RTL texture size.
extern "C" void load_texture(hw_rast *d, const uint8_t *rgb, int dim) {
  static uint8_t mip[8][128][128][3];
  int levels = 0;
  for(int t = dim; t >= 1; t >>= 1) levels++;
  for(int y = 0; y < dim; y++)
    for(int x = 0; x < dim; x++)
      for(int c = 0; c < 3; c++)
        mip[0][y][x][c] = rgb[(y*dim + x)*3 + c];
  for(int L = 1; L < levels; L++) {
    int Dl = dim >> L;
    for(int y = 0; y < Dl; y++)
      for(int x = 0; x < Dl; x++)
        for(int c = 0; c < 3; c++)
          mip[L][y][x][c] = (mip[L-1][2*y][2*x][c]   + mip[L-1][2*y][2*x+1][c]
                           + mip[L-1][2*y+1][2*x][c] + mip[L-1][2*y+1][2*x+1][c]) >> 2;
  }
  for(int L = 0; L < levels; L++) {
    int Dl = dim >> L;
    for(int y = 0; y < Dl; y++)
      for(int x = 0; x < Dl; x++) {
        int bank = ((y & 1) << 1) | (x & 1);
        uint32_t addr = (L << 12) | ((y >> 1) << 6) | (x >> 1);
        uint32_t texel = ((uint32_t)mip[L][y][x][0] << 16)
                       | ((uint32_t)mip[L][y][x][1] << 8) | mip[L][y][x][2];
        hw_tex_write(d, bank, addr, texel);
      }
  }
}
