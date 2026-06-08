# Next steps

Roadmap for the hardware rasterizer. Roughly prioritized; each item notes
where it lands and why.

## Current state

End-to-end OBJ rendering works:

- **`obj.cc`** loads a Wavefront OBJ (`v`/`f`, fan-triangulated).
- **`pipeline.cc`** runs the 3D software front-end: auto-fit, model/view/
  projection, near-plane *reject*, perspective divide, viewport, winding
  fixup, two-sided flat (N·L) shading.
- **`setup.cc`** computes per-triangle edge functions and the depth plane
  (Cramer's rule, normalized once into 25.12 fixed point).
- **`rasterize.sv`** steps coverage + depth across the bounding box.
- **`top.cc`** drives the RTL, applies a software depth test, writes
  `raster2d.ppm`, and reports the simulated clock count.

`bigguy.obj` and `monsterfrog.obj` render correctly with depth occlusion.

The items below are the deferred pieces from the first cut, plus the
natural CRIME-style trajectory of pushing more work into hardware.

## Pipeline correctness & the obvious gaps

1. **Near-plane clipping** (replace reject). Today any triangle touching the
   near plane is dropped. Sutherland–Hodgman against the near plane (a
   triangle can split into a quad → 2 triangles) lets the camera move into
   and through geometry. *`pipeline.cc`.*

2. **Screen-edge / frustum-side clipping.** Currently we rely on auto-fit
   keeping models in frame plus a framebuffer-bounds guard in `top.cc`.
   Clipping (or a guard-band) so off-screen geometry can't waste cycles or
   alias addresses. *`pipeline.cc`, and the bounding-box logic in
   `rasterize.sv`.*

3. **Backface culling.** *Done* — drops back faces by the eye-space normal
   test (`dot(N, e0) >= 0`) before rasterizing; `--no-cull` disables it.
   Measured on bigguy: 2872 → 1437 triangles, ~49% fewer clocks and ~50%
   fewer fragments, visible image unchanged. *`pipeline.cc`.*

## Moving work into hardware (the CRIME trajectory)

4. **Hardware depth buffer + depth test.** The depth buffer currently lives
   in `top.cc`. Moving it on-chip is the SZ read/test/write stage
   (spec §7.3.7.17, Fig 7-17): a depth memory, a comparator, and the
   `LESS`/`LEQUAL`/… functions. This is exactly where the fixed-point,
   cross-triangle-comparable depth pays off — the test becomes a plain `<`.

5. **Profile with the cycle/pixel counters.** `top.cc` reports clocks,
   fragments, and pixels-per-triangle. Backface culling (#3) is measured;
   the overhead left to chase is bounding-box overscan (the scan clocks
   every bbox pixel, ~half outside the triangle) and the ~8–9 fixed setup
   clocks per triangle, which dominate for small triangles (~4 clocks/pixel
   observed).

## Rendering features

6. **Smooth (Gouraud) shading.** Interpolate per-vertex color as a plane
   equation (spec §7.3.7.1) — the same stepper shape as depth. Needs `vn`
   from the OBJ (we skip it today) and a per-component setup in `setup.cc`.

7. **Perspective-correct attributes.** For color/UV interpolation, carry
   `1/w` and `attr/w` and divide per fragment (needs a reciprocal). Note:
   depth is already correct as affine — this is only for attributes.

8. **Texture mapping.** Texel lookup + filtering (spec §7.3.7.6–7.3.7.8),
   on top of #7.

9. **Multiple objects / camera animation.** A scene list and a moving
   camera; dump frames (or drive it via `/loop`) to see motion.

## Repo hygiene

10. **OBJ test assets.** Decide whether `bigguy.obj` / `monsterfrog.obj`
    are committed as fixtures or ignored. The default run needs
    `bigguy.obj` present.

11. **Deterministic depth regression.** The two intersecting-plane
    triangles (known red/green seam) were replaced by model rendering when
    the pipeline landed. Keep a tiny synthetic scene with known expected
    pixels as a fast correctness check.
