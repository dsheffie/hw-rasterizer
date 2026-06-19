---
title: Gouraud shading
nav_order: 7
---

# Gouraud shading — more steppers

Flat shading gives one color per triangle, so the model looks faceted. **Gouraud
shading** lights each *vertex* and smoothly interpolates the color across the
triangle. By now the pattern is familiar: it's three more
[iterated linear functions](index.html), one per color channel.

## The setup

On the host, each vertex is lit by its own normal (transformed to eye space):

```
intensity = ambient + (1 − ambient)·max(0, N·L)
vertex_color = base · intensity
```

That gives a per-vertex `(R, G, B)`. Each channel is then interpolated across the
triangle exactly like depth — a plane with `dC/dx`, `dC/dy` gradients from the
same Cramer's-rule [setup](depth-buffer.html), iterated with an adder.

## Affine, not perspective-correct

Unlike texture coordinates, Gouraud color is interpolated **affine** — linearly
in screen space, with no `1/w` divide. That's how fixed-function OpenGL did it,
and it's cheap: no reciprocal, just three more steppers (`R, G, B`). It's
technically not perspective-correct (color can shear slightly on steeply oblique
triangles), but it's visually fine and matches the GL behavior we're emulating.

## Modulation

The final pixel color combines the interpolated vertex color with the texture:

```
pixel = texel × vertex_color        (per channel, GL_MODULATE)
```

This is exactly `GL_MODULATE` — the texture provides detail, the Gouraud color
provides lighting. (The fixed-point multiply uses an exact `×/255` so a white
vertex color leaves the texel unchanged.)

## In the code

- `pipeline.cc` → per-vertex lighting; `screen_tri` carries `col[3][3]`, one RGB
  per vertex (and the per-vertex colors follow the winding fix-up).
- `rasterize.sv` → the `r_cr / r_cg / r_cb` steppers; the per-pixel value is
  clamped to `[0,255]` and carried through the pipeline's side-band to the
  modulate stage, where it multiplies the (filtered) texel.

Next: [Bilinear filtering & mipmapping](texture-filtering.html) — where the
interesting hardware problem is *memory*, not math.
