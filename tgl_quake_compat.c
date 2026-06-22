// GL/GLU shims for building GLQuake against TinyGL.
//
// GLQuake uses a handful of GL entry points TinyGL doesn't provide.  Most are
// either trivially expressible via TinyGL calls (glOrtho, the GLU helpers, the
// -f texture setters) or no-ops for our purposes (depth func/range, alpha test,
// fog, lightmap sub-uploads) -- "viz can be very wrong" for the first build.

#include <GL/gl.h>
#include "GL/glu.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// --- core GL that TinyGL lacks ---

void glOrtho(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f) {
  GLfloat m[16];
  memset(m, 0, sizeof m);
  m[0]  = 2.0f / (r - l);
  m[5]  = 2.0f / (t - b);
  m[10] = -2.0f / (f - n);
  m[12] = -(GLfloat)((r + l) / (r - l));
  m[13] = -(GLfloat)((t + b) / (t - b));
  m[14] = -(GLfloat)((f + n) / (f - n));
  m[15] = 1.0f;
  glMultMatrixf(m);
}

// glColor3ubv: TinyGL has no ubyte-vector color setter
void glColor3ubv(const GLubyte *v) {
  glColor3f(v[0] / 255.0f, v[1] / 255.0f, v[2] / 255.0f);
}

// the -f variants just forward to TinyGL's integer setters
void glTexParameterf(GLenum target, GLenum pname, GLfloat param) {
  glTexParameteri(target, pname, (GLint)param);
}
void glTexEnvf(GLenum target, GLenum pname, GLfloat param) {
  glTexEnvi(target, pname, (GLint)param);
}

// no-ops: our engine has no alpha test / fog.  (glTexSubImage2D is now real --
// see tinygl/src/texture.c -- so dynamic lightmap updates take effect.)
void glDepthFunc(GLenum func) { (void)func; }
void glDepthRange(GLdouble n, GLdouble f) { (void)n; (void)f; }   /* GLclampd absent in TinyGL */
void glAlphaFunc(GLenum func, GLfloat ref) { (void)func; (void)ref; }  /* GLclampf absent */
void glFogf(GLenum p, GLfloat v) { (void)p; (void)v; }
void glFogi(GLenum p, GLint v) { (void)p; (void)v; }
void glFogfv(GLenum p, const GLfloat *v) { (void)p; (void)v; }

// --- GLU helpers ---

void gluPerspective(GLdouble fovy, GLdouble aspect, GLdouble zNear, GLdouble zFar) {
  GLdouble ymax = zNear * tan(fovy * M_PI / 360.0);
  GLdouble ymin = -ymax;
  GLdouble xmin = ymin * aspect;
  GLdouble xmax = ymax * aspect;
  glFrustum(xmin, xmax, ymin, ymax, zNear, zFar);
}

int gluBuild2DMipmaps(GLenum target, GLint components, GLint width, GLint height,
                      GLenum format, GLenum type, const void *data) {
  // TinyGL builds its own mip chain on TexImage; just upload level 0.
  glTexImage2D(target, 0, components, width, height, 0, format, type, data);
  return 0;
}

// simple nearest-neighbour rescale (handles GL_RGB / GL_RGBA, GL_UNSIGNED_BYTE)
int gluScaleImage(GLenum format, GLint win, GLint hin, GLenum typein, const void *datain,
                  GLint wout, GLint hout, GLenum typeout, void *dataout) {
  (void)typein; (void)typeout;
  int comps = (format == GL_RGBA) ? 4 : 3;
  const unsigned char *src = (const unsigned char *)datain;
  unsigned char *dst = (unsigned char *)dataout;
  for (int y = 0; y < hout; y++) {
    int sy = (int)((long)y * hin / hout);
    for (int x = 0; x < wout; x++) {
      int sx = (int)((long)x * win / wout);
      const unsigned char *s = src + (sy * win + sx) * comps;
      unsigned char *d = dst + (y * wout + x) * comps;
      for (int c = 0; c < comps; c++) d[c] = s[c];
    }
  }
  return 0;
}
