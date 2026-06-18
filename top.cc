#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <fstream>
#if __has_include(<SDL2/SDL.h>)
#include <SDL2/SDL.h>
#else
#include <SDL.h>
#endif
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

// Procedural checkerboard texture: N tiles across the [0,1] uv square,
// tiling (via floor) for uv outside it.  Returns a gray texel.
static void sample_checker(double u, double v, uint8_t &cr, uint8_t &cg, uint8_t &cb) {
  const int N = 8;
  int iu = (int)std::floor(u * N);
  int iv = (int)std::floor(v * N);
  uint8_t shade = ((iu + iv) & 1) ? 40 : 230;
  cr = cg = cb = shade;
}

// Drive the RTL for one triangle, draining fragments through a software
// depth buffer (nearest z wins).  Expects the model in IDLE on entry and
// leaves it in IDLE on return.
static uint64_t render_triangle(Vrasterize *tb, Rgb *fb, int32_t *zbuf,
			    const screen_tri &st, uint64_t &pixels) {
  const vertex3d &v0 = st.v[0], &v1 = st.v[1], &v2 = st.v[2];
  // x/y vertices feed the on-chip bounding-box and edge-delta logic
  tb->v0_x = v0.x; tb->v0_y = v0.y;
  tb->v1_x = v1.x; tb->v1_y = v1.y;
  tb->v2_x = v2.x; tb->v2_y = v2.y;
  uint64_t ticks = 0;
  // host-computed edge weights and depth plane equation
  tri_setup s = setup_triangle(v0, v1, v2);
  tb->w0 = s.w0; tb->w1 = s.w1; tb->w2 = s.w2;
  tb->dzdx = s.dzdx; tb->dzdy = s.dzdy; tb->z_start = s.z_start;

  // Perspective-correct texturing: interpolate u/w, v/w and 1/w as planes
  // (all linear in screen space).  The host computes the plane gradients; the
  // RTL steppers carry them across the bounding box (same shape as depth).
  // The reciprocal + texel lookup are done in software on the values read back.
  attr_plane Psw = setup_attr(v0, v1, v2, st.uv[0][0]*st.invw[0], st.uv[1][0]*st.invw[1], st.uv[2][0]*st.invw[2]);
  attr_plane Ptw = setup_attr(v0, v1, v2, st.uv[0][1]*st.invw[0], st.uv[1][1]*st.invw[1], st.uv[2][1]*st.invw[2]);
  attr_plane Piw = setup_attr(v0, v1, v2, st.invw[0], st.invw[1], st.invw[2]);

  // convert plane (start, dx, dy) to Q.ATTR_FRAC_BITS fixed point for the RTL
  const double S = (double)(1 << 12);                 // ATTR_FRAC_BITS = 12
  const uint64_t SW_MASK = (1ULL << 48) - 1;          // u/w, t/w are 36.12
  const uint32_t IW_MASK = (1U   << 30) - 1;          // 1/w is 18.12
  tb->sw_start = (uint64_t)std::llround(Psw.a_start*S) & SW_MASK;
  tb->dswdx    = (uint64_t)std::llround(Psw.dadx   *S) & SW_MASK;
  tb->dswdy    = (uint64_t)std::llround(Psw.dady   *S) & SW_MASK;
  tb->tw_start = (uint64_t)std::llround(Ptw.a_start*S) & SW_MASK;
  tb->dtwdx    = (uint64_t)std::llround(Ptw.dadx   *S) & SW_MASK;
  tb->dtwdy    = (uint64_t)std::llround(Ptw.dady   *S) & SW_MASK;
  tb->iw_start = (uint32_t)std::llround(Piw.a_start*S) & IW_MASK;
  tb->diwdx    = (uint32_t)std::llround(Piw.dadx   *S) & IW_MASK;
  tb->diwdy    = (uint32_t)std::llround(Piw.dady   *S) & IW_MASK;

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
	// (u,v) recovered in hardware (reciprocal + multiplies), Q16 signed
	double u = (int32_t)tb->u_q / 65536.0;
	double v = (int32_t)tb->v_q / 65536.0;
	uint8_t cr, cg, cb;
	sample_checker(u, v, cr, cg, cb);
	// modulate texel by the flat shade
	fb[addr][0] = (uint8_t)(cr * st.r / 255);
	fb[addr][1] = (uint8_t)(cg * st.g / 255);
	fb[addr][2] = (uint8_t)(cb * st.b / 255);
      }
      tb->pop_frag = 1;
    }
    tb->clk = (~tb->clk) & 1;
    tb->eval();
    ++ticks;
  }
  return ticks;
}

// Live SDL viewer: spin the model and render each frame through the RTL into
// an on-screen framebuffer.  Controls: Esc/Q quit, Space pause, Left/Right
// nudge the yaw.  Returns nonzero if SDL can't open a window (e.g. headless).
static int run_sdl(Vrasterize *tb, Rgb *fb, int32_t *zbuf, int w, int h,
		   const std::vector<model_tri> &mesh, bool cull) {
  if(SDL_Init(SDL_INIT_VIDEO) != 0) {
    std::cerr << "SDL_Init failed: " << SDL_GetError() << " (no display?)\n";
    return 1;
  }
  SDL_Window *win = SDL_CreateWindow("hw-rasterizer",
				     SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, 0);
  SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC);
  SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB24,
				       SDL_TEXTUREACCESS_STREAMING, w, h);

  float yaw = 35.0f, speed = 2.0f;        // degrees per frame
  bool running = true, paused = false;
  while(running) {
    SDL_Event ev;
    while(SDL_PollEvent(&ev)) {
      if(ev.type == SDL_QUIT) running = false;
      else if(ev.type == SDL_KEYDOWN) {
	switch(ev.key.keysym.sym) {
	case SDLK_ESCAPE: case SDLK_q: running = false; break;
	case SDLK_SPACE:  paused = !paused; break;
	case SDLK_LEFT:   yaw -= 5.0f; break;
	case SDLK_RIGHT:  yaw += 5.0f; break;
	}
      }
    }

    // render one frame through the RTL into the framebuffer
    memset(fb, 0x0, w * h * 3);
    for(int i = 0; i < w * h; i++) zbuf[i] = INT32_MAX;
    std::vector<screen_tri> tris = project_mesh(mesh, w, h, cull, yaw);
    uint64_t px = 0;
    for(const screen_tri &t : tris) render_triangle(tb, fb, zbuf, t, px);

    SDL_UpdateTexture(tex, nullptr, fb, w * 3);
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, nullptr, nullptr);
    SDL_RenderPresent(ren);
    if(!paused) yaw += speed;
  }

  SDL_DestroyTexture(tex);
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  SDL_Quit();
  return 0;
}

int main(int argc, char *argv[]) {
  const std::unique_ptr<VerilatedContext> contextp{new VerilatedContext};
  contextp->commandArgs(argc, argv);

  const char *model_path = "bigguy.obj";
  if(argc > 1 && argv[1][0] != '+' && argv[1][0] != '-') model_path = argv[1];

  bool cull = true;                       // backface culling, toggle with --no-cull
  int frames = 1;                         // --spin N: render N frames over 360 deg
  bool sdl = false;                       // --sdl: live spinning viewer
  for(int i = 1; i < argc; i++) {
    if(strcmp(argv[i], "--no-cull") == 0) cull = false;
    else if(strcmp(argv[i], "--spin") == 0 && i + 1 < argc) frames = atoi(argv[++i]);
    else if(strcmp(argv[i], "--sdl") == 0) sdl = true;
  }

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
  std::cout << "model " << model_path << ": " << mesh.size() << " triangles"
	    << (cull ? " (backface cull on)" : " (backface cull off)") << "\n";

  if(sdl) {
    int rc = run_sdl(tb, framebuffer, zbuf, w, h, mesh, cull);
    delete [] framebuffer; delete [] zbuf; delete tb;
    return rc;
  }

  uint64_t ticks = 0, pixels = 0;
  for(int f = 0; f < frames; f++) {
    // fresh framebuffer + depth buffer each frame
    memset(framebuffer, 0x0, w * h * 3);
    for(int i = 0; i < w * h; i++) zbuf[i] = INT32_MAX;

    float yaw = 35.0f + (frames > 1 ? f * (360.0f / frames) : 0.0f);
    std::vector<screen_tri> tris = project_mesh(mesh, w, h, cull, yaw);
    for(const screen_tri &t : tris) {
      ticks += render_triangle(tb, framebuffer, zbuf, t, pixels);
    }

    char name[64];
    if(frames > 1) snprintf(name, sizeof name, "frame_%03d.ppm", f);
    else           snprintf(name, sizeof name, "raster2d.ppm");
    std::ofstream ofs(name);
    ofs << "P6\n" << w << " " << h << "\n255\n";
    ofs.write((char*)framebuffer, w * h * 3);
    ofs.close();
    std::cout << "frame " << f << " (yaw " << yaw << "): " << tris.size()
	      << " tris -> " << name << "\n";
  }

  std::cout << "all done, " << frames << " frame(s), " << ticks << " clocks total\n";

  delete [] framebuffer;
  delete [] zbuf;
  delete tb;

  return 0;
}
