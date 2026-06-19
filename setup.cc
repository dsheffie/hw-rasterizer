#include "setup.h"
#include <algorithm>
#include <cmath>

// Vertex x,y arrive in sub-pixel fixed point (SUBPIXEL units per pixel, 1/32).
// We rasterize whole pixels but evaluate coverage and the interpolation planes
// at the true sub-pixel positions, sampled at each pixel's CENTER -- so edges
// land where the geometry actually is instead of snapping to integer corners.
static const int   SUBPIXEL_BITS = 5;            // matches rasterize.sv (1/32)
static const int   SUBPIXEL      = 1 << SUBPIXEL_BITS;
static const double INV_SUB       = 1.0 / SUBPIXEL;

tri_setup setup_triangle(const vertex3d &v0, const vertex3d &v1, const vertex3d &v2) {
  // bounding-box origin in whole pixels (floor of the sub-pixel min); the same
  // value the RTL derives by min(x)>>5.  Coverage/planes are evaluated at the
  // center of that first pixel.
  int32_t x_min = std::min(std::min(v0.x, v1.x), v2.x) >> SUBPIXEL_BITS;
  int32_t y_min = std::min(std::min(v0.y, v1.y), v2.y) >> SUBPIXEL_BITS;
  int32_t cx = x_min * SUBPIXEL + SUBPIXEL / 2;   // first pixel center, sub-pixel
  int32_t cy = y_min * SUBPIXEL + SUBPIXEL / 2;

  tri_setup s;
  // Edge functions at the pixel center, in sub-pixel^2 units (the cross product
  // of two ×SUBPIXEL vectors). Only the sign matters for coverage; the RTL
  // steps these by whole-pixel deltas it derives from the (×SUBPIXEL) verts.
  // (b−a)×(center−a), with the same edge assignment the RTL expects.
  s.w0 = (int32_t)((int64_t)(v1.x-v0.x)*(cy-v0.y) - (int64_t)(v1.y-v0.y)*(cx-v0.x)); // v0->v1
  s.w1 = (int32_t)((int64_t)(v0.x-v2.x)*(cy-v2.y) - (int64_t)(v0.y-v2.y)*(cx-v2.x)); // v2->v0
  s.w2 = (int32_t)((int64_t)(v2.x-v1.x)*(cy-v1.y) - (int64_t)(v2.y-v1.y)*(cx-v1.x)); // v1->v2

  // Depth plane, in floating *pixel* coordinates so the gradients come out per
  // whole pixel directly (16-bit depth, so double is plenty).
  double xf0 = v0.x*INV_SUB, yf0 = v0.y*INV_SUB;
  double xf1 = v1.x*INV_SUB, yf1 = v1.y*INV_SUB;
  double xf2 = v2.x*INV_SUB, yf2 = v2.y*INV_SUB;
  double area     = xf0*(yf1-yf2) - yf0*(xf1-xf2) + (xf1*yf2 - xf2*yf1);
  double dzdx_num = (double)v0.z*(yf1-yf2) - yf0*(double)(v1.z-v2.z) + ((double)v1.z*yf2 - (double)v2.z*yf1);
  double dzdy_num = xf0*(double)(v1.z-v2.z) - (double)v0.z*(xf1-xf2) + (xf1*(double)v2.z - xf2*(double)v1.z);
  double dzdx = dzdx_num / area;
  double dzdy = dzdy_num / area;

  const double S = (double)(1 << DEPTH_FRAC_BITS);
  s.dzdx = std::llround(dzdx * S);
  s.dzdy = std::llround(dzdy * S);
  // depth at the first pixel center; +0.5 LSB so the RTL's truncation rounds.
  double zc = v0.z + dzdx*((x_min + 0.5) - xf0) + dzdy*((y_min + 0.5) - yf0);
  s.z_start = std::llround(zc * S) + (1 << (DEPTH_FRAC_BITS-1));
  return s;
}

attr_plane setup_attr(const vertex3d &v0, const vertex3d &v1, const vertex3d &v2,
                      double a0, double a1, double a2) {
  int32_t x_min = std::min(std::min(v0.x, v1.x), v2.x) >> SUBPIXEL_BITS;
  int32_t y_min = std::min(std::min(v0.y, v1.y), v2.y) >> SUBPIXEL_BITS;

  // floating pixel coordinates -> per-whole-pixel gradients, value at the center
  double xf0 = v0.x*INV_SUB, yf0 = v0.y*INV_SUB;
  double xf1 = v1.x*INV_SUB, yf1 = v1.y*INV_SUB;
  double xf2 = v2.x*INV_SUB, yf2 = v2.y*INV_SUB;
  double area     = xf0*(yf1-yf2) - yf0*(xf1-xf2) + (xf1*yf2 - xf2*yf1);
  double dadx_num = a0*(yf1-yf2) - yf0*(a1-a2) + (a1*yf2 - a2*yf1);
  double dady_num = xf0*(a1-a2) - a0*(xf1-xf2) + (xf1*a2 - xf2*a1);

  attr_plane p;
  p.dadx = dadx_num / area;
  p.dady = dady_num / area;
  p.a_start = a0 + p.dadx*((x_min + 0.5) - xf0) + p.dady*((y_min + 0.5) - yf0);
  return p;
}
