#ifndef PIPELINE_H
#define PIPELINE_H

#include <vector>
#include <cstdint>
#include "setup.h"   // vertex3d
#include "obj.h"

// A screen-space triangle ready for the rasterizer: integer pixel
// coordinates + quantized depth per vertex, plus a flat-shade color.
struct screen_tri {
  vertex3d v[3];
  uint8_t r, g, b;
};

// Run the 3D software pipeline over a model: model/view/projection
// transform, near-plane reject, optional backface cull, perspective
// divide, viewport mapping, winding fixup (so coverage matches the RTL),
// and flat (N.L) shading.  Produces screen-space triangles to feed the
// rasterizer.
std::vector<screen_tri> project_mesh(const std::vector<model_tri> &mesh,
				     int width, int height, bool cull_backfaces);

#endif
