#ifndef GFX_H
#define GFX_H

// Reusable host-side graphics glue on top of the hw_rast engine abstraction.
// Used by the OBJ demo and (next) the IRIS GL / libgl backend, so the
// per-triangle setup math and the mip-texture upload live in one place.
//
// Plain-C interface (no pipeline.h / setup.h leakage) so a C backend can call it.

#include <stdint.h>
#include "hw_rast.h"

#ifdef __cplusplus
extern "C" {
#endif

// Set up one screen-space triangle (edge functions, depth/attribute/color
// planes) and submit it to the engine.  pos is [vertex][x,y,z] in screen space,
// col is [vertex][r,g,b]; uv + invw drive perspective-correct texturing
// (use uv={0} and invw=1 for untextured).
void submit_triangle(hw_rast *d,
                     const int32_t pos[3][3],
                     const float   uv[3][2],
                     const float   invw[3],
                     const uint8_t col[3][3],
                     uint8_t blend_mode, uint8_t alpha);

// Build the mip chain from a dim x dim RGB image (dim must equal the RTL
// texture dimension) and upload it to the engine's banked texture memory.
void load_texture(hw_rast *d, const uint8_t *rgb, int dim);

#ifdef __cplusplus
}
#endif

#endif
