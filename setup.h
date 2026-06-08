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

#endif
