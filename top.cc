#include <cstdint>
#include <cstring>
#include <iostream>
#include <fstream>
#include "Vrasterize.h"
#include "setup.h"
#include "obj.h"
#include "pipeline.h"

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
static uint64_t render_triangle(Vrasterize *tb, Rgb *fb, int32_t *zbuf,
			    const vertex3d &v0, const vertex3d &v1, const vertex3d &v2,
			    uint8_t r, uint8_t g, uint8_t b, uint64_t &pixels) {
  // x/y vertices feed the on-chip bounding-box and edge-delta logic
  tb->v0_x = v0.x; tb->v0_y = v0.y;
  tb->v1_x = v1.x; tb->v1_y = v1.y;
  tb->v2_x = v2.x; tb->v2_y = v2.y;
  uint64_t ticks = 0;
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
      ++pixels;                                     // a covered fragment emitted by the rasterizer
      int32_t z = (int32_t)tb->pixel_q;             // already true (fixed-point) depth
      uint32_t addr = tb->addr_q;
      // guard: off-screen vertices can produce out-of-range addresses
      if(addr < (uint32_t)(imageWidth*imageHeight) && z < zbuf[addr]) {
	zbuf[addr] = z;                             // depth test: nearest wins
	fb[addr][0] = r;
	fb[addr][1] = g;
	fb[addr][2] = b;
      }
      tb->pop_frag = 1;
    }
    tb->clk = (~tb->clk) & 1;
    tb->eval();
    ++ticks;
  }
  return ticks;
}

int main(int argc, char *argv[]) {
  const std::unique_ptr<VerilatedContext> contextp{new VerilatedContext};
  contextp->commandArgs(argc, argv);

  const char *model_path = "bigguy.obj";
  if(argc > 1 && argv[1][0] != '+' && argv[1][0] != '-') model_path = argv[1];

  bool cull = true;                       // backface culling, toggle with --no-cull
  for(int i = 1; i < argc; i++)
    if(strcmp(argv[i], "--no-cull") == 0) cull = false;

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

  // 3D software pipeline: load the model and project it to screen-space
  // triangles, then rasterize each through the RTL + software depth buffer.
  std::vector<model_tri> mesh = load_obj(model_path);
  if(mesh.empty()) {
    std::cerr << "failed to load model: " << model_path << "\n";
    return 1;
  }
  std::vector<screen_tri> tris = project_mesh(mesh, w, h, cull);
  std::cout << "model " << model_path << ": " << mesh.size()
	    << " triangles, " << tris.size() << " after pipeline"
	    << (cull ? " (backface cull on)" : " (backface cull off)") << "\n";

  uint64_t ticks = 0, pixels = 0;
  for(const screen_tri &t : tris) {
    ticks += render_triangle(tb, framebuffer, zbuf, t.v[0], t.v[1], t.v[2], t.r, t.g, t.b, pixels);
  }

  std::cout << "all done, rendered " << tris.size() << " triangles, trying to write image\n";
  std::cout << "would take " << ticks << " clocks\n";
  std::cout << "rendered " << pixels << " pixels ("
	    << (tris.empty() ? 0.0 : (double)pixels / tris.size()) << " per triangle)\n";
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
