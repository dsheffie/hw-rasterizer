# SGI CRIME Rendering Engine — Notes (from `crime.pdf`, ch. 7)

`crime.pdf` (repo root) is the **SGI CRIME 1.5 SPEC** (Proprietary/Confidential,
dated 6/2/97, codename "Moosehead") — the rendering engine in the SGI O2. 236
pages; **Chapter 7 "Rendering Engine"** is the relevant part (PDF pages ~157–236;
internal page numbers are `7-N`). Several subsections are marked "TBD" — it is an
in-progress internal document.

These notes capture the design facts that motivate the texture-mapping plan
(`docs/texture-mapping-plan.md`).

## Core architecture

- **Rasterization-only accelerator.** The host CPU does transforms, lighting,
  texture-coordinate assignment, and plane-equation setup. The hardware only
  steps linear functions and divides per pixel. (§7.3, p7-4) — the same
  host/hardware split this project already has (`setup.cc` on the host,
  `rasterize.sv` in RTL).

- **Unified plane-equation stepper** (§7.3.7, p7-42/43): `z(x,y) = A·x + B·y + C`
  is evaluated incrementally — compute start value `Zs`, then add `A` (= dz/dx)
  per unit x-step and `B` (= dz/dy) per unit y-step. Here "z" is *any* interpolated
  attribute: R/G/B/A color, homogeneous texture coordinates, fog factor, or depth.
  The host solves for A, B via Cramer's rule (EQ 7-9); the denominator
  `t = (y1-y0)(x2-x1) - (y2-y1)(x1-x0)` is the triangle area. **This is the same
  math already in `setup.cc`.** CRIME simply instantiates the stepper once per
  attribute.

## Perspective-correct texturing

The texel-generation pipeline (Figure 7-15, p7-47) is **fully perspective-correct,
per pixel** — no affine approximation, no subspan correction:

```
Homogeneous Coordinate Stepping  ->  (s/w, t/w, 1/w)
        |
Perspective Division             ->  (s, t)        <- per-pixel divide
        |
(u,v) Generation                 ->  texel coords
        |
LOD computation -> texel lookup -> filtering
```

Registers (Table 7-3 / 7-17): three plane steppers for `s/w`, `t/w`, `1/w` —
`TexGen.sq` (36.12), `TexGen.tq` (36.12), `TexGen.q` (18.12) — plus six slope
registers `dsqdx/dsqdy/dtqdx/dtqdy/dqdx/dqdy`. The 36 *integer* bits on `s/w` and
`t/w` hold the magnitude blow-up of texture coordinates near the camera.

### Why this is affordable on mid-90s hardware

- The expensive divide-by-area (plane-coefficient setup) happens **once per
  triangle, on the host CPU**.
- The per-pixel hardware cost is small: 3 adds (stepping s/w, t/w, 1/w) + one
  reciprocal of 1/w + 2 multiplies.

SGI paid the per-pixel divide rather than approximating it — a real-world existence
proof that perspective-correct mapping is the right choice for a mid-90s-class
design.

## Fixed-point formats

| Attribute | Format |
|-----------|--------|
| depth     | 25.12  |
| color     | 9.12   |
| fog       | 1.20   |
| s/w, t/w  | 36.12  |
| 1/w       | 18.12  |

## Other details

- **Pipeline order** (§7.3.7): shade (flat or Gouraud) → texture (modulate/blend)
  → fog → antialias → alpha test → blend. Texturing composes with shading, it does
  not replace it.
- **Mip-map LOD** (Figure 7-16): from screen-space derivatives
  du/dx, dv/dx, du/dy, dv/dy; `rho = max(...)`; `LOD = log2(rho)`. Maps 1×1 up to
  1K×1K, nearest/linear filtering, clamp/repeat.
- **Texel formats** (Figure 7-7): LA8, A1_RGB5 (1555), RGBA4, RGBA8.
- **No dedicated VRAM**: color/depth/stencil/texture all live in system SDRAM via
  a TLB, including a dedicated 64KB texture-tile TLB (`TLB.tex[28]`).
