#ifndef SETUP_H
#define SETUP_H

#include <cstdint>

// A triangle vertex in screen space with a depth coordinate.
struct vertex3d {
  int32_t x, y, z;
};

// Per-triangle parameters the rasterizer RTL consumes.  All of the
// geometry math (edge functions + depth plane equation) is performed
// here on the host; the hardware only iterates these via its steppers.
struct tri_setup {
  int32_t w0, w1, w2;   // edge weights at the bounding-box start corner
  int32_t dzdx, dzdy;   // depth plane gradients, scaled by 'area'
  int32_t z_start;      // depth at the start corner, scaled by 'area'
  int32_t area;         // 2*signed area; recover depth as pixel_q / area
};

tri_setup setup_triangle(const vertex3d &v0, const vertex3d &v1, const vertex3d &v2);

#endif
