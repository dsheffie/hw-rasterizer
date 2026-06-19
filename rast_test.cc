// Isolation test for the IRIS GL backend (hw_rasterizer.cc): feed rasterizer.h
// a couple of screen-space triangles directly (bypassing gl.c / a demo) and
// dump the result, to validate the engine seam before the full demo link.
#define bool bool   // see hw_rasterizer.cc: dodge basic_types.h's bool typedef
extern "C" {
#include "rasterizer.h"
}
#undef bool
#include <cstdio>
#include <cstring>
#include <cmath>
#if __has_include(<SDL2/SDL.h>)
#include <SDL2/SDL.h>
#else
#include <SDL.h>
#endif

// Present the (BGRA, top-down) front buffer in an SDL window until quit.
static int show_sdl(unsigned char *fb, int w, int h) {
  if(SDL_Init(SDL_INIT_VIDEO) != 0) {
    fprintf(stderr, "SDL_Init failed: %s (no display?)\n", SDL_GetError());
    return 1;
  }
  SDL_Window *win = SDL_CreateWindow("irisgl backend test",
      SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, 0);
  SDL_Renderer *ren = SDL_CreateRenderer(win, -1, 0);
  // frontbuf bytes are B,G,R,A -> little-endian uint32 0xAARRGGBB = ARGB8888
  SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
      SDL_TEXTUREACCESS_STREAMING, w, h);
  bool running = true;
  while(running) {
    SDL_Event ev;
    while(SDL_PollEvent(&ev)) {
      if(ev.type == SDL_QUIT) running = false;
      else if(ev.type == SDL_KEYDOWN &&
              (ev.key.keysym.sym == SDLK_ESCAPE || ev.key.keysym.sym == SDLK_q))
        running = false;
    }
    SDL_UpdateTexture(tex, nullptr, fb, w * 4);
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, nullptr, nullptr);
    SDL_RenderPresent(ren);
    SDL_Delay(16);
  }
  SDL_DestroyTexture(tex);
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  SDL_Quit();
  return 0;
}

static void V(screen_vertex &s, int x, int y, uint32_t z,
              uint8_t r, uint8_t g, uint8_t b) {
  s.x = x * SCREEN_VERTEX_V2_SCALE;
  s.y = y * SCREEN_VERTEX_V2_SCALE;
  s.z = z; s.r = r; s.g = g; s.b = b; s.a = 255;
}

// A fan of thin spokes about a center -- the worst case for vertex snapping.
// snap=false places the outer points at their true sub-pixel position; snap=true
// rounds them to whole pixels (what integer-vertex rasterization sees). Same
// engine both ways, so any difference is purely the sub-pixel coordinates.
static void draw_spokes(float ccx, float ccy, bool snap) {
  const int N = 24;
  const float R = 150.0f;
  for(int k = 0; k < N; k++) {
    float a = k * (3.14159265f * 2.0f / N);
    float ex = ccx + R*cosf(a), ey = ccy + R*sinf(a);
    float px = -sinf(a), py = cosf(a);          // perpendicular, for a thin wedge
    screen_vertex t[3];
    auto put = [&](screen_vertex &s, float x, float y) {
      if(snap) { x = roundf(x); y = roundf(y); }
      s.x = (uint16_t)lroundf(x*32); s.y = (uint16_t)lroundf(y*32);
      s.z = 0x20000000; s.r = 220; s.g = 220; s.b = 230; s.a = 255;
    };
    put(t[0], ccx, ccy);
    put(t[1], ex + px*1.2f, ey + py*1.2f);
    put(t[2], ex - px*1.2f, ey - py*1.2f);
    rasterizer_draw(DRAW_TRIANGLES, 3, t);
  }
}

int main(int argc, char *argv[]) {
  bool sdl = false, spokes = false;
  for(int i = 1; i < argc; i++) {
    if(strcmp(argv[i], "--sdl") == 0) sdl = true;
    else if(strcmp(argv[i], "--spokes") == 0) spokes = true;
  }

  rasterizer_winopen((char*)"irisgl backend test");
  rasterizer_zbuffer(1);
  rasterizer_czclear(0, 0, 0, 0, 0xffffffff);

  if(spokes) {
    draw_spokes(220, 240, false);   // left: true sub-pixel
    draw_spokes(580, 240, true);    // right: integer-snapped vertices
    rasterizer_swap();
    unsigned char *fb = rasterizer_frontbuffer();
    if(sdl) return show_sdl(fb, 800, 480);
    FILE *f = fopen("irisgl_test.ppm", "wb");
    fprintf(f, "P6\n800 480\n255\n");
    for(int i = 0; i < 800*480; i++) { fputc(fb[i*4+2],f); fputc(fb[i*4+1],f); fputc(fb[i*4+0],f); }
    fclose(f);
    printf("wrote irisgl_test.ppm (left=sub-pixel, right=snapped)\n");
    return 0;
  }

  screen_vertex tri[6];
  // near triangle (small z), R/G/B Gouraud corners
  V(tri[0], 100, 100, 0x20000000, 255,   0,   0);
  V(tri[1], 450, 160, 0x20000000,   0, 255,   0);
  V(tri[2], 200, 420, 0x20000000,   0,   0, 255);
  // far triangle (large z), overlaps the first -- depth test should occlude it
  V(tri[3], 250, 120, 0x80000000, 255, 255,   0);
  V(tri[4], 650, 320, 0x80000000,   0, 255, 255);
  V(tri[5], 320, 460, 0x80000000, 255,   0, 255);
  rasterizer_draw(DRAW_TRIANGLES, 6, tri);

  rasterizer_swap();
  unsigned char *fb = rasterizer_frontbuffer();   // BGRA, 800x480, top-down

  if(sdl)
    return show_sdl(fb, 800, 480);

  FILE *f = fopen("irisgl_test.ppm", "wb");
  fprintf(f, "P6\n800 480\n255\n");
  for(int i = 0; i < 800 * 480; i++) {
    fputc(fb[i*4 + 2], f);   // R
    fputc(fb[i*4 + 1], f);   // G
    fputc(fb[i*4 + 0], f);   // B
  }
  fclose(f);
  printf("wrote irisgl_test.ppm\n");
  return 0;
}
