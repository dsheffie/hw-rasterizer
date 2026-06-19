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
#include "gfx.h"
#include "obj.h"
#include "pipeline.h"

#ifndef SCREEN_RES
#define SCREEN_RES 256          // must match the RTL SCREEN_RES (Makefile RES)
#endif
const int32_t imageWidth = SCREEN_RES;
const int32_t imageHeight = SCREEN_RES;

typedef uint8_t Rgb[3];

const int texDim = 128;   // must match TEX_LW (1<<7) in rasterize.sv

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

// Generate the demo's level-0 texture and hand it to the engine (which builds
// the mip chain and uploads it).  Default is broadband tiling value-noise
// "stone"; --grid uses the high-frequency u/v gradient+grid (a worst case for
// aliasing, handy for checking UV mapping).
static void load_demo_texture(hw_rast *d, bool grid) {
  static uint8_t base[128*128*3];
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
      base[(y*texDim + x)*3 + 0] = r;
      base[(y*texDim + x)*3 + 1] = g;
      base[(y*texDim + x)*3 + 2] = b;
    }
  load_texture(d, base, texDim);     // engine builds + uploads the mip chain
}

// Adapt a screen-space triangle to the shared engine front-end (gfx.cc).
static void render_triangle(hw_rast *d, const screen_tri &st,
                            uint8_t blend_mode, uint8_t alpha) {
  int32_t pos[3][3];
  float   uv[3][2], invw[3];
  uint8_t col[3][3];
  for(int i = 0; i < 3; i++) {
    // x,y are whole-pixel here; the engine wants sub-pixel (×32) coords
    pos[i][0] = st.v[i].x * 32; pos[i][1] = st.v[i].y * 32; pos[i][2] = st.v[i].z;
    uv[i][0]  = st.uv[i][0]; uv[i][1] = st.uv[i][1];
    invw[i]   = st.invw[i];
    col[i][0] = st.col[i][0]; col[i][1] = st.col[i][1]; col[i][2] = st.col[i][2];
  }
  submit_triangle(d, pos, uv, invw, col, blend_mode, alpha);
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
  load_demo_texture(d, grid);         // generate + upload the demo texture

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
