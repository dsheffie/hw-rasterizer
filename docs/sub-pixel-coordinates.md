---
title: Sub-pixel coordinates
nav_order: 3
---

# Sub-pixel coordinates

[Coverage](edge-functions.html) decides *which* pixels are inside a triangle. The
next question is *where exactly the edge sits* — and that depends entirely on how
much precision we keep in the vertex positions.

## The problem: snapping to integers

A vertex almost never lands on a pixel. Suppose an edge endpoint is really at
`x = 100.7` and we round it to the nearest pixel:

```c
int x = sv.x / 32;     // 100.7 -> 100, the fraction is thrown away
```

Two things break:

1. **The edge can only fall on whole-pixel grid lines.** A shallow edge that
   should descend 0.3 px per column instead stays flat for three columns and then
   jumps a whole pixel — a coarse staircase that follows the *rounded* vertices,
   not the true slope.
2. **It crawls under motion.** As the model turns, `100.7 → 100.4 → 100.9` all
   snap to `100` or `101`, so the edge twitches by whole pixels frame-to-frame
   instead of sliding smoothly.

## Keep the fraction

Screen vertices arrive in **1/32-pixel fixed point** (5 fractional bits — IRIS
GL's `SCREEN_VERTEX_V2_SCALE = 32`). So `sv.x = 3222` *means* `3222 / 32 =
100.6875`: the integer pixel is `3222 >> 5`, the fraction is the low 5 bits.
Sub-pixel rasterization carries those bits all the way through coverage and
plane setup instead of discarding them.

## Coverage at pixel centers, in full precision

Rasterization asks: *is this pixel's **center** inside the triangle?* — i.e. is
the center on the inside of all three [edge functions](edge-functions.html)? The
whole game is the precision of the endpoints and the sample point.

- **Integer (today):** the `E` functions are built from snapped integer
  vertices and sampled at integer positions. The sign flip — the actual edge —
  can only sit on integer grid lines → jaggies.
- **Sub-pixel:** the `E` functions keep their 5 fraction bits and are sampled at
  the true center. The edge can now be placed to within 1/32 of a pixel.

It stays pure integer math — just scaled up. In the ×32 world a half-pixel is
`16`, so the center of pixel `(px, py)` is:

```
center = (px·32 + 16,  py·32 + 16)
```

You evaluate the edge functions, built from the ×32 vertex coords, at that point.
The bounding box is the integer pixels that *enclose* the sub-pixel triangle:
`xmin = floor(min_x / 32)`, `xmax = ceil(max_x / 32)`.

## The inner loop doesn't change

This is the nice part. Sub-pixel only enriches the **setup** values; the
[forward-differenced](edge-functions.html) sweep is identical. Moving one whole
pixel still adds a constant to each edge function (now `±32·Δ` in the scaled
space). So:

- the **start value** carries the extra fraction bits (and the `+16` center
  offset),
- the **per-pixel step** is still one add per edge,

and the iteration costs exactly what it did before. We just feed it richer
numbers.

## Two more payoffs

- **Watertight shared edges.** Two triangles that share an edge share the *exact*
  fractional endpoints, so the edge function is bit-identical for both. With a
  consistent fill rule every pixel along the seam is owned by exactly one
  triangle — no cracks, no doubled lines. Integer snapping lets the two
  triangles round the shared edge differently, producing gaps or double-draws.
- **Better depth and attributes.** The [depth](depth-buffer.html) and attribute
  planes are built from the vertex coordinates; truncating the vertices tilts
  those planes slightly. Sub-pixel setup makes the interpolated z and colors
  match the true surface — which also reduces z-fighting between near-coplanar
  surfaces.

## What it is *not*

Sub-pixel positioning still produces **hard, aliased edges**: each pixel is fully
lit or fully off. It fixes *where* the staircase sits and stops it crawling, but
it doesn't soften it. Smoothing the edge is **antialiasing** (coverage / MSAA /
SSAA) — a separate feature, though it builds directly on sub-pixel coverage,
since AA needs to know how much of each pixel the edge crosses.

## Status in this design

Our engine currently takes **integer vertices**: `hw_rasterizer.cc` does the
`sv.x / 32` truncation above, and `setup.cc`'s edge/plane setup runs on integer
coordinates. That's the source of the ragged edges visible when we run the SGI
[IRIS GL demos](toward-minigl.html). The fix is mechanical and matches the
pattern above: stop truncating, set up the edge functions in ×32 space (the
determinants grow by `32²`, so widen the fixed-point), and start the sweep at the
first pixel center `(xmin·32+16, ymin·32+16)`.

Next: [Barycentric coordinates](barycentric.html) — the same edge functions,
normalized, become the bridge from coverage to interpolation.
