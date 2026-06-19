---
title: Toward miniGL
nav_order: 9
---

# Toward miniGL

GLQuake never needed full OpenGL — it ran on a small "miniGL" subset that id and
3dfx wired up for the Voodoo. That's the target: not GL conformance, but the
specific subset that runs GLQuake-class content.

## What the pages so far cover

The [iterated-linear-function](index.html) spine plus a couple of memories
already give the core of that subset:

- **coverage** — [edge functions / Pineda](edge-functions.html)
- **hidden surfaces** — [iterated depth + the depth buffer](depth-buffer.html)
- **perspective-correct texturing** — [`u/w, t/w, 1/w` + reciprocal](perspective-texturing.html)
- **smooth lighting** — [Gouraud](gouraud-shading.html), combined with the
  texture via `GL_MODULATE`
- **filtering** — [bilinear + mipmapping](texture-filtering.html)
- all of it **on-chip**, fed by a [one-fragment-per-cycle pipeline](hardware-pipeline.html)

GLQuake notably does **not** need most of what's hard in GL: no fixed-function
GL lighting (Quake bakes lighting into lightmaps), no stencil, no fog (in base
GLQuake), no points/lines for gameplay.

## What's left

- **Blending** — for translucency (water, particles), and the 2D HUD/menu
  overlay. The color buffer becomes a read-modify-write, structurally identical
  to the depth test: read the destination, blend, write back.
- **Lightmaps** — every Quake world surface is `texture × lightmap`. Either a
  second texture multiplied in one pass (multitexture), or a multiply-blend
  second pass — both reuse the texturing and blending machinery.
- **A miniGL driver** on the ARM — turns the handful of GL calls GLQuake makes
  (immediate-mode vertices, texture binds, blend/depth state, matrices) into the
  per-triangle setup the PL consumes. This is the bulk of the *software* still to
  write, but it's a bounded subset — that's why a small miniGL shipped on the
  Voodoo in the first place.

## The recurring shape

Every feature on this path turns out to be one of three moves:

1. **another iterated linear function** (a stepper) — coverage, depth,
   attributes, color, fog;
2. **another memory** (a BRAM, banked/cached) — depth buffer, framebuffer,
   texture, lightmap;
3. **host-side glue** — setup math, the driver.

That's the whole architecture, all the way up from a 2D triangle.
