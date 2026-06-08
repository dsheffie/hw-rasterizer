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

  // depth plane equation: z*area = dzdx*x + dzdy*y + c, stepped by the RTL
  s.area    = v0.x*(v1.y-v2.y) - v0.y*(v1.x-v2.x) + (v1.x*v2.y - v2.x*v1.y);
  s.dzdx    = v0.z*(v1.y-v2.y) - v0.y*(v1.z-v2.z) + (v1.z*v2.y - v2.z*v1.y);
  s.dzdy    = v0.x*(v1.z-v2.z) - v0.z*(v1.x-v2.x) + (v1.x*v2.z - v2.x*v1.z);
  s.z_start = v0.z*s.area + s.dzdx*(xs-v0.x) + s.dzdy*(ys-v0.y);
  return s;
}
