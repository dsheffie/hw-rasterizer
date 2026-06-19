---
title: Edge functions & Pineda's algorithm
nav_order: 2
---

# Edge functions and Pineda's algorithm

The first question is *coverage*: which pixels are inside a triangle? The answer
is the first of our [iterated linear functions](index.html).

## The edge function

Take a directed edge from vertex `A` to vertex `B`. For any point `P`, the
**edge function** is the 2D cross product of the edge vector with the vector to
`P`:

```
E_AB(P) = (B − A) × (P − A)
        = (B.x − A.x)·(P.y − A.y) − (B.y − A.y)·(P.x − A.x)
```

Geometrically this is **twice the signed area** of the triangle `A, B, P`. Its
*sign* tells you which side of the infinite line through `A→B` the point `P` is
on:

- `E_AB(P) > 0` → `P` is on one side (left, for a counter-clockwise edge)
- `E_AB(P) < 0` → the other side
- `E_AB(P) = 0` → exactly on the line

## Inside = same side of all three edges

Walk the three edges consistently (`V0→V1`, `V1→V2`, `V2→V0`). A point is
**inside** the triangle exactly when it's on the same side of all three edges —
i.e. all three edge functions share the same sign. With a fixed winding you can
just test "are all three non-negative?" (equivalently, "are all three sign bits
zero?"). That's **Pineda's algorithm**: evaluate three edge functions per pixel,
combine their sign tests.

## Worked example

Triangle `V0 = (1,1)`, `V1 = (5,1)`, `V2 = (1,4)` (a right triangle). Edge
`V0→V1` has `B − A = (4, 0)`, so

```
E_01(P) = 4·(P.y − 1) − 0·(P.x − 1) = 4·(P.y − 1)
```

- At `P = (2,2)` (inside): `E_01 = 4·(2−1) = 4 > 0`.
- At `P = (2,0)` (below the bottom edge): `E_01 = 4·(0−1) = −4 < 0` → outside.

The other two edges work the same way; a pixel is drawn only if all three edge
functions are ≥ 0.

## The key move: iterate, don't recompute

`E_AB` is linear in `P`, so it forward-differences. Step `P` by `+1` in `x` or
`y`:

```
E_AB(x+1, y) − E_AB(x, y) = (B − A) × x̂ = −(B.y − A.y) = −Δy
E_AB(x, y+1) − E_AB(x, y) = (B − A) × ŷ = +(B.x − A.x) = +Δx
```

So the per-pixel update of each edge function is **add a constant** — the edge's
`−Δy` going right, `+Δx` going down. No multiplies in the inner loop, just three
adders (one per edge).

## How we scan

Compute the triangle's **bounding box**, evaluate the three edge functions once
at the box's start corner (the only place the cross products are computed), then
sweep:

```
for each row y in the bbox:
    w0, w1, w2 = row-start values
    for each x in the bbox:
        if (w0 ≥ 0 and w1 ≥ 0 and w2 ≥ 0): emit fragment at (x, y)
        w0 += dE0/dx;  w1 += dE1/dx;  w2 += dE2/dx        # step right
    advance the row-start values by the dE/dy gradients     # next row
```

Sweeping the whole bounding box wastes the pixels outside the triangle (roughly
half) — that's the first thing a tiled or edge-walking scanner would optimize.

## In the code

- `setup.cc` → `cross(a, b, p)` is the edge function. (It's written
  `(b−a)×(p−b)`, which is algebraically identical to `(b−a)×(p−a)` because
  `(b−a)×(a−b) = 0`.) `setup_triangle()` evaluates `w0, w1, w2` at the
  bounding-box start corner.
- `rasterize.sv` → the registers `r_w0, r_w1, r_w2` (with row-restart copies
  `r_w0_y` …) are the iterated edge functions. In the `RENDER` state the x-step
  subtracts the edge's `Δy` and the row-step subtracts its `Δx`. Coverage is the
  single line `(r_w0[31] | r_w1[31] | r_w2[31]) == 0` — no sign bit set = inside.

Next: [Sub-pixel coordinates](sub-pixel-coordinates.html) — how much precision we
keep in the vertex positions decides where these edges actually land.
