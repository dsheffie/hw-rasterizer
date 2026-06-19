---
title: Iterated depth & the depth buffer
nav_order: 5
---

# Iterated `z` and the depth buffer

Now hidden-surface removal. Each vertex has a screen-space depth `z`. We want
`z` at every covered pixel so that, when two triangles cover the same pixel, the
nearer one wins. It's our first interpolated attribute — and it's another
[iterated linear function](index.html).

## The depth plane

`z` across the triangle is a plane:

```
z(x, y) = z0 + (dz/dx)·(x − x0) + (dz/dy)·(y − y0)
```

The two gradients come from solving the plane through the three `(x, y, z)`
vertices — a 3×3 system solved by **Cramer's rule**. The shared denominator is
the same `area` (twice the signed triangle area) from the
[edge functions](edge-functions.html):

```
area    = x0·(y1−y2) − y0·(x1−x2) + (x1·y2 − x2·y1)
dz/dx   = [ z0·(y1−y2) − y0·(z1−z2) + (z1·y2 − z2·y1) ] / area
dz/dy   = [ x0·(z1−z2) − z0·(x1−x2) + (x1·z2 − x2·z1) ] / area
```

That single divide-by-`area` is the "expensive setup" we push to the host. After
it, `z` is iterated with the same forward-difference adder as the edge functions.

## Why depth is exact as an affine interpolation

Perspective projection divides by depth, so it warps most quantities — but the
*stored* depth is special. Window depth has the form `z_ndc = A + B/Z` (where
`Z` is eye-space depth), and `1/Z` is itself affine in screen space (see
[perspective texturing](perspective-texturing.html)), so `z_ndc` is affine in
screen space too. That means plain linear interpolation of `z` across the
triangle is **exact** — no perspective correction needed (unlike texture
coordinates).

## Fixed point

The host normalizes `dz/dx, dz/dy, z_start` once into `Q25.12` fixed point
(`DEPTH_FRAC_BITS = 12`). Carrying *true, normalized* depth — rather than raw
edge weights — means the depth test is a plain integer `<`, and depths are
comparable across triangles. (`setup.cc`, `setup_triangle()`.)

## The depth buffer

For each fragment:

```
old = zbuf[pixel]
if (new_z < old):        # nearest wins
    zbuf[pixel] = new_z
    ... shade and write the color ...
```

That's a read-modify-write on an on-chip BRAM. Within one triangle every pixel
address is unique, so a short pipeline has no read-after-write hazard; across
triangles we drain the pipe between triangles, so there's none there either.
(This same RMW shape returns later for alpha blending — the color buffer becomes
read-modify-write too.)

## In the code

- `setup.cc` → the depth plane (the Cramer's-rule block) and `z_start`.
- `rasterize.sv` → the `r_z` / `r_z_y` stepper (load `z_start` at `go`, add
  `dzdx` across a row, reload `row + dzdy` between rows — structurally identical
  to the edge steppers), the `r_zb` depth BRAM, the `Z1` depth-test stage that
  absorbs the BRAM read latency, and a clear FSM that resets the buffer per
  frame.

Next: [Perspective-correct texture coordinates](perspective-texturing.html) —
where the "screen-affine only" caveat finally bites, and how we get around it.
