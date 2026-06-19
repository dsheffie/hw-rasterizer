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
#include "hw_rast.h"
#include "setup.h"
#include "obj.h"
#include "pipeline.h"

#ifndef SCREEN_RES
#define SCREEN_RES 256          // must match the RTL SCREEN_RES (Makefile RES)
#endif
const int32_t imageWidth = SCREEN_RES;
const int32_t imageHeight = SCREEN_RES;

typedef uint8_t Rgb[3];

const int texDim = 128;   // must match TEX_LW (1<<7) in rasterize.sv
const int texLevels = 8;  // must match MIP_LEVELS (TEX_LW+1)

// integer hash -> [0,1], used for tiling value noise
static float vhash(int x, int y) {
  uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return (float)(h & 0xffffff) / (float)0xffffff;
}
// value noise that tiles seamlessly with the given period (must divide texDim)
static float vnoise(float x, float y, int period) {
  int cells = texDim / period;
  int x0 = (int)std::floor(x / period), y0 = (int)std::floor(y / period);
  float tx = x / period - x0, ty = y / period - y0;
  tx = tx*tx*(3-2*tx); ty = ty*ty*(3-2*ty);              // smoothstep
  auto at = [&](int cx, int cy) {
    return vhash(((cx % cells) + cells) % cells, ((cy % cells) + cells) % cells);
  };
  float a = at(x0,y0), b = at(x0+1,y0), c = at(x0,y0+1), d = at(x0+1,y0+1);
  return (a + (b-a)*tx)*(1-ty) + (c + (d-c)*tx)*ty;
}

// Build a mipmapped texture and load it.  Default level 0 is broadband tiling
// value-noise "stone"; --grid uses the high-frequency u/v gradient+grid (a
// worst case for aliasing, handy for checking UV mapping).  Coarser levels are
// box-filter downsamples, uploaded to bank {y&1,x&1} at the uniform 64x64 slot.
static void load_mipmapped_texture(hw_rast *d, bool grid) {
  static uint8_t mip[8][128][128][3];
  for(int y = 0; y < texDim; y++)
    for(int x = 0; x < texDim; x++) {
      uint8_t r, g, b;
      if(grid) {
        r = x * 2; g = y * 2; b = 128;
        if((x & 15) == 0 || (y & 15) == 0) { r = g = b = 0; }
      } else {
        float n = 0.55f*vnoise(x,y,32) + 0.30f*vnoise(x,y,16) + 0.15f*vnoise(x,y,8);
        r = (uint8_t)(120 + n*110);   // warm stone
        g = (uint8_t)(105 + n*95);
        b = (uint8_t)(85  + n*70);
      }
      mip[0][y][x][0] = r; mip[0][y][x][1] = g; mip[0][y][x][2] = b;
    }
  for(int L = 1; L < texLevels; L++) {
    int D = texDim >> L;
    for(int y = 0; y < D; y++)
      for(int x = 0; x < D; x++)
        for(int c = 0; c < 3; c++)
          mip[L][y][x][c] = (mip[L-1][2*y][2*x][c]   + mip[L-1][2*y][2*x+1][c]
                           + mip[L-1][2*y+1][2*x][c] + mip[L-1][2*y+1][2*x+1][c]) >> 2;
  }
  for(int L = 0; L < texLevels; L++) {
    int D = texDim >> L;
    for(int y = 0; y < D; y++)
      for(int x = 0; x < D; x++) {
        int bank = ((y & 1) << 1) | (x & 1);
        uint32_t addr = (L << 12) | ((y >> 1) << 6) | (x >> 1);
        uint32_t rgb = ((uint32_t)mip[L][y][x][0] << 16)
                     | ((uint32_t)mip[L][y][x][1] << 8) | mip[L][y][x][2];
        hw_tex_write(d, bank, addr, rgb);
      }
  }
}

// Set up one screen-space triangle (edge functions, depth/attribute/color
// planes -- all the per-triangle math) and submit it to the engine.  The
// LOD is computed per-pixel in hardware, so no LOD is sent.
static void render_triangle(hw_rast *d, const screen_tri &st,
                            uint8_t blend_mode, uint8_t alpha) {
  const vertex3d &v0 = st.v[0], &v1 = st.v[1], &v2 = st.v[2];
  tri_setup s = setup_triangle(v0, v1, v2);

  // perspective-correct attribute planes (u/w, t/w, 1/w) and Gouraud color
  attr_plane Psw = setup_attr(v0, v1, v2, st.uv[0][0]*st.invw[0], st.uv[1][0]*st.invw[1], st.uv[2][0]*st.invw[2]);
  attr_plane Ptw = setup_attr(v0, v1, v2, st.uv[0][1]*st.invw[0], st.uv[1][1]*st.invw[1], st.uv[2][1]*st.invw[2]);
  attr_plane Piw = setup_attr(v0, v1, v2, st.invw[0], st.invw[1], st.invw[2]);
  attr_plane Pcr = setup_attr(v0, v1, v2, st.col[0][0], st.col[1][0], st.col[2][0]);
  attr_plane Pcg = setup_attr(v0, v1, v2, st.col[0][1], st.col[1][1], st.col[2][1]);
  attr_plane Pcb = setup_attr(v0, v1, v2, st.col[0][2], st.col[1][2], st.col[2][2]);

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

// Live SDL viewer: spin the model and render each frame through the RTL into
// an on-screen framebuffer.  Controls: Esc/Q quit, Space pause, Left/Right
// nudge the yaw.  Returns nonzero if SDL can't open a window (e.g. headless).
static int run_sdl(hw_rast *d, Rgb *fb, int w, int h,
		   const std::vector<model_tri> &mesh, bool cull,
		   uint8_t blend_mode, uint8_t alpha) {
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
    hw_clear(d);
    std::vector<screen_tri> tris = project_mesh(mesh, w, h, cull, yaw);
    for(const screen_tri &t : tris) render_triangle(d, t, blend_mode, alpha);
    hw_fb_read(d, (uint8_t*)fb);

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
  const char *model_path = "bigguy.obj";
  if(argc > 1 && argv[1][0] != '+' && argv[1][0] != '-') model_path = argv[1];

  bool cull = true;                       // backface culling, toggle with --no-cull
  int frames = 1;                         // --spin N: render N frames over 360 deg
  bool sdl = false;                       // --sdl: live spinning viewer
  bool grid = false;                      // --grid: debug uv gradient+grid texture
  bool blend = false;                     // --blend: translucent (src-over) demo
  for(int i = 1; i < argc; i++) {
    if(strcmp(argv[i], "--no-cull") == 0) cull = false;
    else if(strcmp(argv[i], "--spin") == 0 && i + 1 < argc) frames = atoi(argv[++i]);
    else if(strcmp(argv[i], "--sdl") == 0) sdl = true;
    else if(strcmp(argv[i], "--grid") == 0) grid = true;
    else if(strcmp(argv[i], "--blend") == 0) { blend = true; cull = false; }
  }

  hw_rast *d = hw_open(imageWidth, imageHeight);
  load_mipmapped_texture(d, grid);    // one-time mipmapped texture upload

  // blend state (uniform for the whole model here)
  uint8_t blend_mode = blend ? 1 : 0; // 1 = src-over
  uint8_t blend_alpha = 128;

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
    int rc = run_sdl(d, framebuffer, w, h, mesh, cull, blend_mode, blend_alpha);
    delete [] framebuffer; hw_close(d);
    return rc;
  }

  for(int f = 0; f < frames; f++) {
    hw_clear(d);                        // clear color + depth each frame

    float yaw = 35.0f + (frames > 1 ? f * (360.0f / frames) : 0.0f);
    std::vector<screen_tri> tris = project_mesh(mesh, w, h, cull, yaw);
    for(const screen_tri &t : tris)
      render_triangle(d, t, blend_mode, blend_alpha);
    hw_fb_read(d, (uint8_t*)framebuffer);

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

  std::cout << "all done, " << frames << " frame(s)\n";

  delete [] framebuffer;
  hw_close(d);

  return 0;
}
