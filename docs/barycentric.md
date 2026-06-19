---
title: Barycentric coordinates
nav_order: 3
---

# Barycentric coordinates (the edge functions, normalized)

The three [edge functions](edge-functions.html) aren't just sign tests.
**Normalized, they are the barycentric coordinates** of the pixel — and that is
the bridge from *coverage* to *interpolation*.

## From edge functions to weights

If `area` is twice the signed triangle area (the cross product of two edges),
then

```
λ0 = E_12(P) / area     λ1 = E_20(P) / area     λ2 = E_01(P) / area
λ0 + λ1 + λ2 = 1
```

Each `λ_i` is the fractional "weight" of vertex `i` at point `P`: at vertex 0,
`λ0 = 1` and the others are 0; on the opposite edge, `λ0 = 0`.

## Interpolating an attribute

To interpolate any per-vertex attribute `a` across the triangle, take the
weighted sum:

```
a(P) = λ0·a0 + λ1·a1 + λ2·a2
```

Because the `λ_i` are linear in `(x,y)` (they're scaled edge functions), `a(P)`
is also linear in `(x,y)` — **so it forward-differences too**. Every attribute
we interpolate (depth, `u/w`, color …) is "just another edge-function-shaped
plane," set up once and iterated with an adder. This is why the same hardware
shape — a start value plus two gradients, stepped — shows up again and again.

## The catch: only valid for screen-affine quantities

Barycentric interpolation in *screen* space is only *correct* for quantities
that are themselves affine in screen space. Screen `x`, `y`, the NDC depth `z`,
and `1/w` are affine in screen space. The raw texture coordinate `u` is **not** —
perspective warps it — which is exactly what
[perspective-correct texturing](perspective-texturing.html) fixes by
interpolating `u/w` and `1/w` (which *are* affine) and dividing.

## We don't actually divide per pixel

In practice we never compute `λ_i` and re-weight per pixel. The normalization
(`/ area`) is folded into the precomputed gradients during
[setup](depth-buffer.html) — so the per-pixel cost stays a single add. The
barycentric view is the *why*; the plane-equation stepper is the *how*.

Next: [Iterated depth & the depth buffer](depth-buffer.html) — the first real
interpolated attribute.
