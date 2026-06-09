# Texture Mapping — Plan

Direction for adding texture mapping to the rasterizer. Modeled on the SGI CRIME
rendering engine (see `docs/crime-rendering-engine-notes.md`), which is a
real-world existence proof for this approach in a mid-90s-class part.

## Decision

**Option B: hardware UV interpolation, perspective-correct** — not affine-only,
not subspan.

The concern that "1/w is expensive for mid-90s hardware" resolves the same way
CRIME resolved it: the expensive divide-by-triangle-area (plane setup) is offloaded
to the host; the per-pixel hardware cost is only 3 adds + one reciprocal + 2
multiplies.

Rationale: the existing depth pipeline already solves the same plane-interpolation
problem once, so texturing reuses that machinery.

## Steps

1. **`obj.cc` / `obj.h`** — currently discards `vt` texture coordinates and the
   `vn` index in `f` tokens; parse and store them.
2. **`setup.cc`** — reuse the existing Cramer's-rule plane setup (it is the same as
   CRIME EQ 7-9) to compute plane coefficients for three new attributes: `s/w`,
   `t/w`, `1/w`. This requires carrying `1/w` through `pipeline.cc`, which currently
   discards it at the perspective divide (cf. `docs/next-steps.md`).
3. **`rasterize.sv`** — instantiate three more steppers alongside the existing
   `Depth.dzdx/dzdy/z_start` stepper, for `s/w`, `t/w`, `1/w`. Suggested fixed-point
   per CRIME: s/w, t/w as 36.12; 1/w as 18.12.
4. **Perspective-division unit (new hardware)** — reciprocal of `1/w` + two
   multiplies → recover `(s, t)` → scale to `(u, v)`. This is the one genuinely new
   block; everything upstream already exists for depth.
5. **`top.cc`** — load a texture (PPM), do the texel lookup per fragment, modulate
   with the shaded color, gated by the existing depth test.

## Staging

Get the steppers carrying `s/w`, `t/w`, `1/w` first and validate against affine
output, **then** drop in the reciprocal unit for perspective-correct mapping. This
mirrors CRIME's `(s/w, t/w, 1/w) → divide → (s, t)` path.

## Current codebase facts (confirmed during exploration)

- Vertices carry only position + quantized depth.
- Color is flat per-triangle (N·L lighting in `pipeline.cc`).
- Only depth is interpolated today.
- Output is 512×512 P6 PPM.
- Build: Verilator + clang++ via Makefile (`make`, then
  `./hw_rasterize [model.obj] [--no-cull]`).
