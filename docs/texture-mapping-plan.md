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

## Math reference

### Per-pixel datapath

Iterate three attributes that are linear in screen space (so each is a cheap
adder-stepper, like the existing depth stepper):

    a = u/w     b = t/w     c = 1/w

Recover the true texture coords with **one shared reciprocal** per pixel:

    approx(1/c) ≈ w
    u = a · approx(1/c) = (u/w)·w
    t = b · approx(1/c) = (t/w)·w

Per-pixel cost is one reciprocal + two multiplies (not two divides). The
reciprocal's relative error multiplies both `u` and `t` identically, so the two
coords stay mutually consistent.

You **cannot** skip the reciprocal by stepping `w` directly: `w` is not linear in
screen space (see below), so it has no constant per-pixel delta. You must step
`c = 1/w` (linear) and invert per pixel.

### Why `1/w` is linear in screen space but `w` is not

Eye space: a triangle point is `(X, Y, Z)`; projection is the divide by depth, so
screen coords are `x_s = X/Z`, `y_s = Y/Z`, and `w = Z`.

Every triangle point lies on one plane: `p·X + q·Y + r·Z = s`. Divide that plane
equation by `Z`:

    p·(X/Z) + q·(Y/Z) + r = s·(1/Z)
    p·x_s   + q·y_s   + r = s·(1/Z)
    =>  1/Z = (p/s)·x_s + (q/s)·y_s + (r/s)

The right side is **affine in `(x_s, y_s)`** — constant slopes — so `1/w = 1/Z`
interpolates linearly across the triangle (exactly what a plane-equation stepper
needs).

Consequently `w = Z = 1 / (affine)` is a reciprocal of an affine function —
rational, not affine — so it has no cheap stepper. (Exception: a fronto-parallel
triangle, `p = q = 0`, where `Z` is constant.)

Same argument for any eye-space-affine attribute `f = αX + βY + γZ + δ`:
`f/Z = α·x_s + β·y_s + γ + δ·(1/Z)` is affine in screen space, while the bare `f`
is `affine/affine` (rational) — which is why interpolating `u` directly (affine
texture mapping) warps, and why we interpolate `u/w` and divide.

This is the same reason the existing depth stepper is exact: stored NDC depth has
the form `z_ndc = A + B/Z`, affine in `1/Z`, hence linear in screen space.
