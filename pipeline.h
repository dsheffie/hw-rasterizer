#ifndef PIPELINE_H
#define PIPELINE_H

#include <vector>
#include <cstdint>
#include "setup.h"   // vertex3d
#include "obj.h"

// A screen-space triangle ready for the rasterizer: integer pixel
// coordinates + quantized depth per vertex, plus a flat-shade color.
// For perspective-correct texturing each vertex also carries its texture
// coordinate and 1/w (clip-space w reciprocal); the rasterizer interpolates
// u/w, v/w and 1/w linearly in screen space and divides per fragment.
struct screen_tri {
  vertex3d v[3];
  float uv[3][2];   // (u,v) per vertex
  float invw[3];    // 1/w per vertex
  uint8_t r, g, b;
};

// Run the 3D software pipeline over a model: model/view/projection
// transform, near-plane reject, optional backface cull, perspective
// divide, viewport mapping, winding fixup (so coverage matches the RTL),
// and flat (N.L) shading.  Produces screen-space triangles to feed the
// rasterizer.
std::vector<screen_tri> project_mesh(const std::vector<model_tri> &mesh,
				     int width, int height, bool cull_backfaces,
				     float yaw_deg = 35.0f);

#endif
