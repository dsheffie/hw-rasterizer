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

const int32_t imageWidth = 256;
const int32_t imageHeight = 256;

typedef uint8_t Rgb[3];

static inline void tick(Vrasterize *tb) {
  tb->clk = (~tb->clk) & 1;
  tb->eval();
  tb->clk = (~tb->clk) & 1;
  tb->eval();
}

// Reset the on-chip color + depth buffers (clear sweep).  Model must be IDLE.
static void clear_buffers(Vrasterize *tb) {
  tb->clear = 1; tick(tb); tb->clear = 0;
  while(tb->clearing) tick(tb);
}

// Drive the RTL for one triangle.  The hardware does depth test, checkerboard
// texturing and shade modulation and writes the survivors straight into the
// on-chip framebuffer.  Expects the model IDLE on entry/return.
static uint64_t render_triangle(Vrasterize *tb, const screen_tri &st) {
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

  // per-triangle flat shade color (packed R,G,B) the RTL modulates the texel by
  tb->tri_rgb = ((uint32_t)st.r << 16) | ((uint32_t)st.g << 8) | st.b;

  tb->go = 1; tick(tb); tb->go = 0;
  while(!tb->done) { tick(tb); ++ticks; }
  return ticks;
}

// Scan the on-chip framebuffer out into a host RGB buffer (registered read:
// set address, clock, then read the data).
static void readout_framebuffer(Vrasterize *tb, Rgb *fb, int n) {
  for(int a = 0; a < n; a++) {
    tb->fb_raddr = a;
    tick(tb);
    uint32_t c = tb->fb_rdata;
    fb[a][0] = (c >> 16) & 0xff;
    fb[a][1] = (c >> 8) & 0xff;
    fb[a][2] = c & 0xff;
  }
}

// Live SDL viewer: spin the model and render each frame through the RTL into
// an on-screen framebuffer.  Controls: Esc/Q quit, Space pause, Left/Right
// nudge the yaw.  Returns nonzero if SDL can't open a window (e.g. headless).
static int run_sdl(Vrasterize *tb, Rgb *fb, int w, int h,
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

    // render one frame: clear on-chip buffers, rasterize, scan the FB out
    clear_buffers(tb);
    std::vector<screen_tri> tris = project_mesh(mesh, w, h, cull, yaw);
    for(const screen_tri &t : tris) render_triangle(tb, t);
    readout_framebuffer(tb, fb, w * h);

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
  tb->clear = 0;
  tb->fb_raddr = 0;
  tb->x_dim = imageWidth;
  tb->y_dim = imageHeight;

  tick(tb);
  tb->rst = 0;
  tick(tb);

  const int w = imageWidth, h = imageHeight;
  Rgb *framebuffer = new Rgb[w * h];
  memset(framebuffer, 0x0, w * h * 3);

  // depth buffer now lives on-chip (BRAM); cleared per frame via clear_zbuffer
  std::vector<model_tri> mesh = load_obj(model_path);
  if(mesh.empty()) {
    std::cerr << "failed to load model: " << model_path << "\n";
    return 1;
  }
  std::cout << "model " << model_path << ": " << mesh.size() << " triangles"
	    << (cull ? " (backface cull on)" : " (backface cull off)") << "\n";

  if(sdl) {
    int rc = run_sdl(tb, framebuffer, w, h, mesh, cull);
    delete [] framebuffer; delete tb;
    return rc;
  }

  uint64_t ticks = 0;
  for(int f = 0; f < frames; f++) {
    // clear the on-chip color + depth buffers each frame
    clear_buffers(tb);

    float yaw = 35.0f + (frames > 1 ? f * (360.0f / frames) : 0.0f);
    std::vector<screen_tri> tris = project_mesh(mesh, w, h, cull, yaw);
    for(const screen_tri &t : tris) {
      ticks += render_triangle(tb, t);
    }
    readout_framebuffer(tb, framebuffer, w * h);

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
  delete tb;

  return 0;
}
