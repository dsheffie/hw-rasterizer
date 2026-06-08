#include "setup.h"
#include <algorithm>

namespace {
  struct vertex2d {
    int32_t x, y;
  };

  // (b-a) x (p-b)
  int32_t cross(const vertex2d &a, const vertex2d &b, const vertex2d &p) {
    vertex2d ab{b.x - a.x, b.y - a.y};
    vertex2d ap{p.x - b.x, p.y - b.y};
    return (ab.x * ap.y) - (ab.y * ap.x);
  }
}

tri_setup setup_triangle(const vertex3d &v0, const vertex3d &v1, const vertex3d &v2) {
  // start point = upper-left corner of the bounding box
  int32_t xs = std::min(std::min(v0.x, v1.x), v2.x);
  int32_t ys = std::min(std::min(v0.y, v1.y), v2.y);
  vertex2d p{xs, ys};

  tri_setup s;
  // edge functions evaluated at the start corner (ordering matches the RTL)
  s.w0 = cross({v0.x, v0.y}, {v1.x, v1.y}, p);
  s.w1 = cross({v2.x, v2.y}, {v0.x, v0.y}, p);
  s.w2 = cross({v1.x, v1.y}, {v2.x, v2.y}, p);

  // depth plane through the three (x,y,z) vertices, solved by Cramer's rule:
  //   area     = 2 * signed triangle area (the shared determinant)
  //   dz*_num  = numerators of dz/dx and dz/dy
  int64_t area     = (int64_t)v0.x*(v1.y-v2.y) - (int64_t)v0.y*(v1.x-v2.x) + ((int64_t)v1.x*v2.y - (int64_t)v2.x*v1.y);
  int64_t dzdx_num = (int64_t)v0.z*(v1.y-v2.y) - (int64_t)v0.y*(v1.z-v2.z) + ((int64_t)v1.z*v2.y - (int64_t)v2.z*v1.y);
  int64_t dzdy_num = (int64_t)v0.x*(v1.z-v2.z) - (int64_t)v0.z*(v1.x-v2.x) + ((int64_t)v1.x*v2.z - (int64_t)v2.x*v1.z);

  // Normalize once per triangle into Q.DEPTH_FRAC_BITS fixed point, so the
  // RTL stepper carries true depth: bounded width and comparable across
  // triangles (no per-fragment divide).
  s.dzdx = (dzdx_num << DEPTH_FRAC_BITS) / area;
  s.dzdy = (dzdy_num << DEPTH_FRAC_BITS) / area;

  // depth at the start corner; +0.5 LSB so the RTL's fraction truncation
  // behaves as round-to-nearest.
  int64_t z0_fx = (int64_t)v0.z << DEPTH_FRAC_BITS;
  s.z_start = z0_fx + s.dzdx*(xs-v0.x) + s.dzdy*(ys-v0.y) + (1 << (DEPTH_FRAC_BITS-1));
  return s;
}
