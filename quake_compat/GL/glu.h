// Minimal GLU header for building GLQuake against TinyGL (which ships no GLU).
// Only the entry points GLQuake actually uses; implemented in tgl_quake_compat.c.
#ifndef _TGL_COMPAT_GLU_H_
#define _TGL_COMPAT_GLU_H_

#include <GL/gl.h>

#ifdef __cplusplus
extern "C" {
#endif

void  gluPerspective(GLdouble fovy, GLdouble aspect, GLdouble zNear, GLdouble zFar);
int   gluBuild2DMipmaps(GLenum target, GLint components, GLint width, GLint height,
                        GLenum format, GLenum type, const void *data);
int   gluScaleImage(GLenum format, GLint widthin, GLint heightin, GLenum typein,
                    const void *datain, GLint widthout, GLint heightout, GLenum typeout,
                    void *dataout);

#ifdef __cplusplus
}
#endif
#endif
