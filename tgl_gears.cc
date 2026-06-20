// TinyGL "gears" on our HW engine.
//
// Geometry/draw are the classic Brian Paul gears (via TinyGL's Raw_Demos), but
// the framebuffer is OURS: TinyGL transforms/lights/clips and emits screen-space
// triangles, our tgl_engine backend rasterizes them on the (Verilated) engine,
// and we scan the engine framebuffer out to a PPM.  This validates the full
// GL -> our-rasterizer seam (incl. TinyGL's near-plane clipping) on x86.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#if __has_include(<SDL2/SDL.h>)
#include <SDL2/SDL.h>
#else
#include <SDL.h>
#endif
extern "C" {
#include "GL/gl.h"
#include "zbuffer.h"
}
#include "gfx.h"
#include "hw_rast.h"

extern "C" void tgl_set_device(hw_rast *d);

#ifndef M_PI
#define M_PI 3.14159265
#endif

static void gear(GLfloat inner_radius, GLfloat outer_radius, GLfloat width, GLint teeth, GLfloat tooth_depth) {
  GLint i;
  GLfloat r0, r1, r2, angle, da, u, v, len;
  r0 = inner_radius;
  r1 = outer_radius - tooth_depth / 2.0;
  r2 = outer_radius + tooth_depth / 2.0;
  da = 2.0 * M_PI / teeth / 4.0;
  glNormal3f(0.0, 0.0, 1.0);
  /* front face */
  glBegin(GL_QUAD_STRIP);
  for (i = 0; i <= teeth; i++) {
    angle = i * 2.0 * M_PI / teeth;
    glVertex3f(r0 * cos(angle), r0 * sin(angle), width * 0.5);
    glVertex3f(r1 * cos(angle), r1 * sin(angle), width * 0.5);
    glVertex3f(r0 * cos(angle), r0 * sin(angle), width * 0.5);
    glVertex3f(r1 * cos(angle + 3 * da), r1 * sin(angle + 3 * da), width * 0.5);
  }
  glEnd();
  /* front sides of teeth */
  glBegin(GL_QUADS);
  for (i = 0; i < teeth; i++) {
    angle = i * 2.0 * M_PI / teeth;
    glVertex3f(r1 * cos(angle), r1 * sin(angle), width * 0.5);
    glVertex3f(r2 * cos(angle + da), r2 * sin(angle + da), width * 0.5);
    glVertex3f(r2 * cos(angle + 2 * da), r2 * sin(angle + 2 * da), width * 0.5);
    glVertex3f(r1 * cos(angle + 3 * da), r1 * sin(angle + 3 * da), width * 0.5);
  }
  glEnd();
  glNormal3f(0.0, 0.0, -1.0);
  /* back face */
  glBegin(GL_QUAD_STRIP);
  for (i = 0; i <= teeth; i++) {
    angle = i * 2.0 * M_PI / teeth;
    glVertex3f(r1 * cos(angle), r1 * sin(angle), -width * 0.5);
    glVertex3f(r0 * cos(angle), r0 * sin(angle), -width * 0.5);
    glVertex3f(r1 * cos(angle + 3 * da), r1 * sin(angle + 3 * da), -width * 0.5);
    glVertex3f(r0 * cos(angle), r0 * sin(angle), -width * 0.5);
  }
  glEnd();
  /* back sides of teeth */
  glBegin(GL_QUADS);
  for (i = 0; i < teeth; i++) {
    angle = i * 2.0 * M_PI / teeth;
    glVertex3f(r1 * cos(angle + 3 * da), r1 * sin(angle + 3 * da), -width * 0.5);
    glVertex3f(r2 * cos(angle + 2 * da), r2 * sin(angle + 2 * da), -width * 0.5);
    glVertex3f(r2 * cos(angle + da), r2 * sin(angle + da), -width * 0.5);
    glVertex3f(r1 * cos(angle), r1 * sin(angle), -width * 0.5);
  }
  glEnd();
  /* outward faces of teeth */
  glBegin(GL_QUAD_STRIP);
  for (i = 0; i < teeth; i++) {
    angle = i * 2.0 * M_PI / teeth;
    glVertex3f(r1 * cos(angle), r1 * sin(angle), width * 0.5);
    glVertex3f(r1 * cos(angle), r1 * sin(angle), -width * 0.5);
    u = r2 * cos(angle + da) - r1 * cos(angle);
    v = r2 * sin(angle + da) - r1 * sin(angle);
    len = sqrt(u * u + v * v);
    u /= len; v /= len;
    glNormal3f(v, -u, 0.0);
    glVertex3f(r2 * cos(angle + da), r2 * sin(angle + da), width * 0.5);
    glVertex3f(r2 * cos(angle + da), r2 * sin(angle + da), -width * 0.5);
    glNormal3f(cos(angle), sin(angle), 0.0);
    glVertex3f(r2 * cos(angle + 2 * da), r2 * sin(angle + 2 * da), width * 0.5);
    glVertex3f(r2 * cos(angle + 2 * da), r2 * sin(angle + 2 * da), -width * 0.5);
    u = r1 * cos(angle + 3 * da) - r2 * cos(angle + 2 * da);
    v = r1 * sin(angle + 3 * da) - r2 * sin(angle + 2 * da);
    glNormal3f(v, -u, 0.0);
    glVertex3f(r1 * cos(angle + 3 * da), r1 * sin(angle + 3 * da), width * 0.5);
    glVertex3f(r1 * cos(angle + 3 * da), r1 * sin(angle + 3 * da), -width * 0.5);
    glNormal3f(cos(angle), sin(angle), 0.0);
  }
  glVertex3f(r1 * cos(0), r1 * sin(0), width * 0.5);
  glVertex3f(r1 * cos(0), r1 * sin(0), -width * 0.5);
  glEnd();
  /* inside radius cylinder */
  glBegin(GL_QUAD_STRIP);
  for (i = 0; i <= teeth; i++) {
    angle = i * 2.0 * M_PI / teeth;
    glNormal3f(-cos(angle), -sin(angle), 0.0);
    glVertex3f(r0 * cos(angle), r0 * sin(angle), -width * 0.5);
    glVertex3f(r0 * cos(angle), r0 * sin(angle), width * 0.5);
  }
  glEnd();
}

static GLfloat view_rotx = 20.0, view_roty = 30.0;
static GLint gear1, gear2, gear3;
static GLfloat angle = 0.0;

static void draw() {
  glPushMatrix();
  glRotatef(view_rotx, 1.0, 0.0, 0.0);
  glRotatef(view_roty, 0.0, 1.0, 0.0);
  glPushMatrix(); glTranslatef(-3.0, -2.0, 0.0); glRotatef(angle, 0,0,1); glCallList(gear1); glPopMatrix();
  glPushMatrix(); glTranslatef(3.1, -2.0, 0.0); glRotatef(-2.0*angle-9.0, 0,0,1); glCallList(gear2); glPopMatrix();
  glPushMatrix(); glTranslatef(-3.1, 4.2, 0.0); glRotatef(-2.0*angle-25.0, 0,0,1); glCallList(gear3); glPopMatrix();
  glPopMatrix();
}

static void initScene() {
  static GLfloat pos[4]   = {5, 5, 10, 0.0};
  static GLfloat red[4]   = {1.0, 0.0, 0.0, 0.0};
  static GLfloat green[4] = {0.0, 1.0, 0.0, 0.0};
  static GLfloat blue[4]  = {0.0, 0.0, 1.0, 0.0};
  static GLfloat white[4] = {1.0, 1.0, 1.0, 0.0};
  glLightfv(GL_LIGHT0, GL_POSITION, pos);
  glLightfv(GL_LIGHT0, GL_DIFFUSE, white);
  glEnable(GL_CULL_FACE);
  glEnable(GL_LIGHT0);
  gear1 = glGenLists(1); glNewList(gear1, GL_COMPILE); glMaterialfv(GL_FRONT, GL_DIFFUSE, blue);  glColor3fv(blue);  gear(1.0, 4.0, 1.0, 20, 0.7); glEndList();
  gear2 = glGenLists(1); glNewList(gear2, GL_COMPILE); glMaterialfv(GL_FRONT, GL_DIFFUSE, red);   glColor3fv(red);   gear(0.5, 2.0, 2.0, 10, 0.7); glEndList();
  gear3 = glGenLists(1); glNewList(gear3, GL_COMPILE); glMaterialfv(GL_FRONT, GL_DIFFUSE, green); glColor3fv(green); gear(1.3, 2.0, 0.5, 10, 0.7); glEndList();
}

// Live SDL viewer: spin the gears, render each frame through the engine into an
// on-screen framebuffer.  Esc/Q quit, Space pause.  Returns nonzero if SDL can't
// open a window (e.g. headless).
static int run_sdl(hw_rast *dev, int w, int h) {
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    fprintf(stderr, "SDL_Init failed: %s (no display?)\n", SDL_GetError());
    return 1;
  }
  SDL_Window *win = SDL_CreateWindow("tgl_gears (HW engine)",
      SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, 0);
  SDL_Renderer *ren = SDL_CreateRenderer(win, -1, 0);
  SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB24,
      SDL_TEXTUREACCESS_STREAMING, w, h);
  static uint8_t rgb[640 * 480 * 3];

  bool running = true, paused = false;
  while (running) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      if (ev.type == SDL_QUIT) running = false;
      else if (ev.type == SDL_KEYDOWN) {
        if (ev.key.keysym.sym == SDLK_ESCAPE || ev.key.keysym.sym == SDLK_q) running = false;
        else if (ev.key.keysym.sym == SDLK_SPACE) paused = !paused;
      }
    }
    if (!paused) angle += 2.0;
    hw_clear(dev);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    draw();
    hw_fb_read(dev, rgb);

    SDL_UpdateTexture(tex, nullptr, rgb, w * 3);
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, nullptr, nullptr);
    SDL_RenderPresent(ren);
  }
  SDL_DestroyTexture(tex);
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  SDL_Quit();
  return 0;
}

int main(int argc, char **argv) {
  int w = 640, h = 480;
  int nframes = 1;
  bool sdl = false;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--frames") && i + 1 < argc) nframes = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--sdl")) sdl = true;
  }

  // our engine + a white texture so the texel*color modulate passes vertex color
  hw_rast *dev = hw_open(w, h);
  tgl_set_device(dev);
  static uint8_t white_tex[128 * 128 * 3];
  memset(white_tex, 0xff, sizeof white_tex);
  load_texture(dev, white_tex, 128);

  // TinyGL: real software ZBuffer is allocated for state, but unused for pixels
  ZBuffer *zb = ZB_open(w, h, ZB_MODE_RGBA, 0);
  if (!zb) { fprintf(stderr, "ZB_open failed\n"); return 1; }
  glInit(zb);

  glClearColor(0.0, 0.0, 0.0, 0.0);
  glViewport(0, 0, w, h);
  glShadeModel(GL_SMOOTH);
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_LIGHTING);
  GLfloat aspect = (GLfloat)h / (GLfloat)w;
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glFrustum(-1.0, 1.0, -aspect, aspect, 5.0, 60.0);
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  glTranslatef(0.0, 0.0, -45.0);

  initScene();

  if (sdl) {
    int rc = run_sdl(dev, w, h);
    ZB_close(zb);
    hw_close(dev);
    return rc;
  }

  static uint8_t rgb[640 * 480 * 3];
  for (int f = 0; f < nframes; f++) {
    angle += 2.0;
    hw_clear(dev);                                   // clear OUR color+depth
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    draw();                                          // -> tgl_engine -> our engine
    hw_fb_read(dev, rgb);                            // scan OUR framebuffer out

    char name[64];
    snprintf(name, sizeof name, "tgl_gears%04d.ppm", f);
    FILE *fp = fopen(name, "wb");
    if (fp) { fprintf(fp, "P6\n%d %d\n255\n", w, h); fwrite(rgb, 1, w * h * 3, fp); fclose(fp); }
    fprintf(stderr, "frame %d -> %s\n", f, name);
  }

  ZB_close(zb);
  hw_close(dev);
  return 0;
}
