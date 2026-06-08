#include <cstdint>
#include <cstring>
#include <iostream>
#include <fstream>
#include "Vrasterize.h"
#include "setup.h"

const int32_t imageWidth = 512;
const int32_t imageHeight = 512;

typedef uint8_t Rgb[3];

static inline void tick(Vrasterize *tb) {
  tb->clk = (~tb->clk) & 1;
  tb->eval();
  tb->clk = (~tb->clk) & 1;
  tb->eval();
}

// Drive the RTL for one triangle, draining fragments through a software
// depth buffer (nearest z wins).  Expects the model in IDLE on entry and
// leaves it in IDLE on return.
static void render_triangle(Vrasterize *tb, Rgb *fb, int32_t *zbuf,
			    const vertex3d &v0, const vertex3d &v1, const vertex3d &v2,
			    uint8_t r, uint8_t g, uint8_t b) {
  // x/y vertices feed the on-chip bounding-box and edge-delta logic
  tb->v0_x = v0.x; tb->v0_y = v0.y;
  tb->v1_x = v1.x; tb->v1_y = v1.y;
  tb->v2_x = v2.x; tb->v2_y = v2.y;

  // host-computed edge weights and depth plane equation
  tri_setup s = setup_triangle(v0, v1, v2);
  tb->w0 = s.w0; tb->w1 = s.w1; tb->w2 = s.w2;
  tb->dzdx = s.dzdx; tb->dzdy = s.dzdy; tb->z_start = s.z_start;

  tb->go = 1; tick(tb); tb->go = 0;

  bool done = false;
  while(not(done)) {
    tb->clk = (~tb->clk) & 1;
    tb->pop_frag = 0;
    tb->eval();
    if(tb->done) {
      done = true;
    }
    if(tb->valid_q) {
      int32_t z = (int32_t)tb->pixel_q;             // already true (fixed-point) depth
      int32_t addr = tb->addr_q;
      if(z < zbuf[addr]) {                          // depth test: nearest wins
	zbuf[addr] = z;
	fb[addr][0] = r;
	fb[addr][1] = g;
	fb[addr][2] = b;
      }
      tb->pop_frag = 1;
    }
    tb->clk = (~tb->clk) & 1;
    tb->eval();
  }
}

int main(int argc, char *argv[]) {
  const std::unique_ptr<VerilatedContext> contextp{new VerilatedContext};
  contextp->commandArgs(argc, argv);

  Vrasterize *tb = new Vrasterize;
  tb->clk = 0;
  tb->rst = 1;
  tb->go  = 0;
  tb->pop_frag = 0;
  tb->x_dim = imageWidth;
  tb->y_dim = imageHeight;

  tick(tb);
  tb->rst = 0;
  tick(tb);

  const int w = imageWidth, h = imageHeight;
  Rgb *framebuffer = new Rgb[w * h];
  memset(framebuffer, 0x0, w * h * 3);

  // software depth buffer: nearest (smallest z) wins
  int32_t *zbuf = new int32_t[w * h];
  for(int i = 0; i < w * h; i++) zbuf[i] = INT32_MAX;

  // Two triangles sharing the same screen footprint but with opposite
  // depth gradients, so their planes intersect: red is nearer along the
  // v0-v1 edge, green is nearer at v2.  Red is drawn first, so a correct
  // depth buffer yields a red/green seam (not an all-green paint-over).
  render_triangle(tb, framebuffer, zbuf,
		  {60,60,10}, {260,100,10}, {100,260,60}, 255,   0, 0);
  render_triangle(tb, framebuffer, zbuf,
		  {60,60,60}, {260,100,60}, {100,260,10},   0, 255, 0);

  std::cout << "all done, trying to write image\n";
  std::ofstream ofs;
  ofs.open("raster2d.ppm");
  ofs << "P6\n" << w << " " << h << "\n255\n";
  ofs.write((char*)framebuffer, w * h * 3);
  ofs.close();

  delete [] framebuffer;
  delete [] zbuf;
  delete tb;

  return 0;
}
