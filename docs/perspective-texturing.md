---
title: Perspective-correct texture coordinates
nav_order: 6
---

# Perspective-correct `u, v` (and `1/w`)

Texturing needs the texture coordinate `(u, v)` at each pixel. The trap:
**`u` is not affine in screen space**, so interpolating it with
[barycentrics](barycentric.html) warps the texture — the classic "affine texture
swim" of early consoles, where textures slide and shear across triangles.

## Why `1/w` is affine but `u` is not

In eye space a triangle point is `(X, Y, Z)`; projection makes screen coords
`x_s = X/Z`, `y_s = Y/Z`, and `w = Z`. Every point of the triangle lies on one
eye-space plane `p·X + q·Y + r·Z = s`. Divide that plane equation by `Z`:

```
p·(X/Z) + q·(Y/Z) + r = s·(1/Z)
p·x_s   + q·y_s   + r = s·(1/Z)
   ⇒  1/Z = (p/s)·x_s + (q/s)·y_s + (r/s)
```

The right-hand side is **affine in `(x_s, y_s)`** — constant slopes — so
`1/w = 1/Z` interpolates linearly across the triangle. The same argument shows
that for any eye-space-affine attribute `f`, the quantity `f/Z` is affine in
screen space, while the bare `f` is `affine / affine` (rational) — not directly
interpolatable. That's the whole story: interpolate the things that are linear,
and divide.

## The fix: interpolate `u/w, t/w, 1/w`, then divide

So we iterate **three more planes** — `u/w`, `t/w`, and `1/w` — using the same
Cramer's-rule [setup](depth-buffer.html) as depth and the same adder steppers.
Then per pixel we recover the true coordinates with **one reciprocal and two
multiplies**:

```
w   = 1 / (1/w)              ← the per-pixel reciprocal
u   = (u/w) · w
v   = (t/w) · w
```

This reciprocal is the *only* genuinely new per-pixel operation beyond adders.
Everything upstream — the three steppers — is the depth machinery copied three
times. Note the single reciprocal is shared by both `u` and `v`, so any error in
it cancels in the ratio and the two coordinates stay mutually consistent.

You **cannot** shortcut this by stepping `w` directly: `w` is `1/affine`
(rational), so it has no constant per-pixel delta. You must step `1/w` (which is
affine) and invert per pixel.

## The reciprocal

`1/w` is computed in hardware by **Newton-Raphson** on a normalized mantissa:

1. range-normalize the input to `[1, 2)` — a leading-zero count + shift;
2. look up a seed `≈ 1/m` in an 8-bit-indexed ROM (good to ~8 bits);
3. one Newton step `y ← y·(2 − d·y)` roughly **doubles** the good bits → ~16-bit
   accuracy, which is plenty.

The unit is pipelined (a few cycles of latency, one result per cycle).
(`recip.sv`; the bit-exact software model is `recip_fixed()` in `top.cc`, and
`recip_tb.cc` checks them against each other over a million inputs.)

## In the code

- `setup.cc` → `setup_attr()` builds the plane for *any* screen-affine attribute
  (the same determinant as depth), used for `u/w, t/w, 1/w`.
- `rasterize.sv` → the `r_sw, r_tw, r_iw` steppers; the `recip` instance; the
  per-fragment multiply + variable shift that recovers `(u, v)` in fixed point.

Next: [Gouraud shading](gouraud-shading.html) — another set of steppers, but
back to the simple affine case.
