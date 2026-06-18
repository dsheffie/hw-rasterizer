#ifndef SETUP_H
#define SETUP_H

#include <cstdint>

// Depth is interpolated in signed fixed-point with this many fractional
// bits (the rest integer).  Must match DEPTH_FRAC_BITS in rasterize.sv.
static const int DEPTH_FRAC_BITS = 12;

// A triangle vertex in screen space with a depth coordinate.
struct vertex3d {
  int32_t x, y, z;
};

// Per-triangle parameters the rasterizer RTL consumes.  All of the
// geometry math (edge functions + depth plane equation) is performed
// here on the host; the hardware only iterates these via its steppers.
struct tri_setup {
  int32_t w0, w1, w2;   // edge weights at the bounding-box start corner
  int64_t dzdx, dzdy;   // depth plane gradients, Q<int>.DEPTH_FRAC_BITS
  int64_t z_start;      // depth at the start corner, same fixed-point format
};

tri_setup setup_triangle(const vertex3d &v0, const vertex3d &v1, const vertex3d &v2);

// A screen-space-linear attribute interpolated as a plane.  Its value at a
// pixel (x,y) is  a_start + dadx*(x-xs) + dady*(y-ys)  where (xs,ys) is the
// bounding-box start corner (min x, min y of the three vertices) -- the same
// corner the rasterizer steps from.
struct attr_plane {
  double a_start, dadx, dady;
};

// Set up the plane through the three vertices for an attribute that is linear
// in screen space, given its per-vertex values (a0,a1,a2).  Same Cramer's-rule
// determinant as the depth plane.  Used to interpolate u/w, v/w and 1/w for
// perspective-correct texturing.
attr_plane setup_attr(const vertex3d &v0, const vertex3d &v1, const vertex3d &v2,
                      double a0, double a1, double a2);

#endif
