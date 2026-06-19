---
title: Bilinear filtering & mipmapping
nav_order: 7
---

# Texture filtering — and why it's a memory-banking problem

With [perspective-correct `(u,v)`](perspective-texturing.html) in hand, we read
the texture from on-chip BRAM, with `REPEAT` wrap (a low-bit mask, since the
texture is power-of-two). Point sampling — snap to the nearest texel — works, but
looks blocky up close and shimmers far away. Fixing both is mostly about
*memory access*, which is the real scarce resource in this kind of hardware:
compute is cheap, parallel memory ports are not.

## Bilinear filtering (magnification)

Instead of snapping to one texel, sample the **2×2 texels** around the sample
point and blend them by the sub-texel fractions `(fx, fy)`:

```
top = lerp(T00, T10, fx)        # lerp(a,b,f) = a + (b−a)·f
bot = lerp(T01, T11, fx)
out = lerp(top, bot, fy)
```

That smooths magnified textures (the "GLQuake looks smooth vs. software Quake's
chunky pixels" difference). Bilinear only helps *magnification* — see mipmapping
below for the *minification* case.

## The banking trick: four texels in one cycle

A BRAM gives you a fixed number of ports (one or two). Reading four texels needs
four accesses — naively four cycles. The fix is **banking**: split the texture
into **4 banks by `(x&1, y&1)`**. Then any aligned 2×2 quad has exactly one texel
in each bank:

```
   bank(x,y) = (y&1)·2 + (x&1)
   a 2×2 quad spans both x-parities and both y-parities
   ⇒ one texel per bank ⇒ all four read in parallel, conflict-free
```

A small **crossbar** then un-permutes the four banks back into `T00/T10/T01/T11`
(which bank holds which corner depends on the base parity `(i0&1, j0&1)`), and
the three lerps run. This is the heart of "it's really a game of banking
memories" — the lerps are the easy 10%, the conflict-free 4-bank fetch is the
real design.

## Mipmapping (minification)

When a surface is far or oblique, one pixel covers *many* texels, and point/
bilinear sampling undersamples — the texture aliases and **shimmers/pops** as the
model moves. The fix is a **mip chain**: pre-averaged downsampled copies of the
texture (128→64→32→…→1), choosing the level where texel:pixel ≈ 1:1, so a
minified surface reads an already-averaged small level instead of undersampling
the full-resolution one. (**Trilinear** filtering also lerps between the two
nearest levels so the level transition itself doesn't pop.)

Mipmapping is also the **bandwidth** enabler: picking the right level keeps the
bilinear 2×2 quad spatially *local*, which is what makes a small texture cache
(and the off-chip bandwidth budget) actually work.

### LOD = the derivative of the iterated functions

The level of detail is set by how fast `(u, v)` changes per screen pixel — the
**Jacobian** `∂(u,v)/∂(x,y)`. Because we *know* the mapping analytically, we
don't have to estimate it by sampling neighbors (as a GPU's `ddx/ddy` does, or as
optical flow estimates motion); we derive it. With `P = u/w`, `W = 1/w` and their
constant per-triangle screen gradients (the stepper deltas), the quotient rule
gives:

```
∂u/∂x = w·(∂P/∂x − u·∂W/∂x)        (and similarly for ∂u/∂y, ∂v/∂x, ∂v/∂y)
```

— every term is already in the pipeline (`w`, `u`, `v`, and the constant
gradients). Then `LOD = log2(longest footprint axis)`, and `log2` is just the
position of the leading bit — the same leading-zero primitive the
[reciprocal](perspective-texturing.html) uses.

The implementation computes the LOD **per pixel** in hardware via the formula
above: at each fragment it forms the four partials from the constant per-triangle
gradients and the per-pixel `u, v, w`, takes the largest, and `clz`'s the
footprint to get the level. (An earlier version computed one LOD per *triangle*
on the host — fine for tiny triangles, but on large faces at varying grazing
angles, e.g. a torus, adjacent faces picked different levels and the texture
detail jumped at every seam. Per-pixel LOD makes the level vary smoothly and
fixes that.)

## In the code

- `rasterize.sv` → the 4 texture banks (`r_tex0..3`), the per-bank quad address
  generation, the un-swizzle crossbar, the `lerp8` blend, and the mip chain
  stacked in the banks (a uniform slot per level so a bank address is
  `{level, row>>1, col>>1}`).
- `top.cc` → builds the mip chain by box-filter downsampling, uploads it to the
  banks, and computes the per-triangle LOD.

Next: [The hardware pipeline](hardware-pipeline.html) — how all these stages are
kept fed.
