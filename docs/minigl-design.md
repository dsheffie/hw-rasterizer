# miniGL design: running OpenGL (and friends) on the engine

Status: design / planning. This doc scopes the software stack that turns real GL
applications — GLGears today, **GLQuake** as the north star — into triangles our
hardware rasterizer draws. It is the plan we build against; code follows.

See also: [Toward miniGL](toward-minigl.html), [Real OpenGL via TinyGL](opengl-via-tinygl.html),
[On-chip baseline + GLQuake target](next-steps.html).

---

## 1. The one fixed point: the engine seam

Everything converges on a single hardware-facing entry point. The host computes,
per triangle, screen-space vertices + plane equations and calls:

```
submit_triangle(dev, pos[3][3], uv[3][2], invw[3], col[3][3], blend_mode, alpha, z_enable)
        → gfx.cc (setup_triangle / setup_attr)  →  hw_tri  →  hw_rast
        → hw_rast_verilated.cc  →  rasterize.sv  (coverage, depth, texture, Gouraud, blend)
        →  on-chip color + Z framebuffer  →  scan-out
```

The engine is a **rasterizer**: it takes screen-space triangles and produces
shaded, depth-tested, textured, blended pixels. It does *not* do transform,
lighting, or clipping. That split is deliberate and is the whole organizing
principle of this doc (see §3).

What the engine accepts today (the contract every front-end targets):

| Field | Meaning |
|---|---|
| `pos[i] = {x, y, z}` | screen-space vertex: x,y in 1/32 **sub-pixel**, z = 24-bit depth (smaller = nearer, clear = max) |
| `uv[i]`, `invw[i]` | texture coords and 1/w for **perspective-correct** interpolation |
| `col[i]` | per-vertex RGB for **Gouraud**, modulated with the texel (`GL_MODULATE`) |
| `blend_mode` | 0 opaque, 1 src-over, 2 additive (color buffer is a read-modify-write) |
| `z_enable` | per-triangle depth test on/off (painter's vs z-buffered) |

Engine capabilities already in silicon-ish RTL: edge-function coverage, sub-pixel
coords, 24-bit on-chip Z + test, perspective-correct texturing with HW reciprocal,
bilinear + mipmap (banked texture memory), Gouraud, src-over/additive blend, clear.

---

## 2. Three front-ends, one seam (current state)

The seam already has three independent producers. The miniGL work is the third.

| Front-end | How geometry is produced | Path to the seam | State |
|---|---|---|---|
| **OBJ demo** | bespoke SW T&L (`pipeline.cc`) | `obj_demo.cc` → `submit_triangle` | done |
| **IRIS GL** | SGI's `libgl` (sgi-demos) | `hw_rasterizer.cc` implements `rasterizer.h` → `submit_triangle` | done (ideas, logo, jello, …) |
| **OpenGL** | **TinyGL** (transform/light/clip) | `tgl_engine.cc` implements TinyGL's `ZB_*` → `submit_triangle` | **gears works; GLQuake is the target** |

The lesson from the first two: *implement the GL's low-level rasterizer hook on
our engine and reuse the entire GL above it unmodified.* The OpenGL path applies
the same trick to TinyGL.

---

## 3. Architecture decision: TinyGL as the miniGL, swap only the rasterizer

**Decision (made):** vendor [TinyGL](https://github.com/C-Chads/tinygl) (a small
OpenGL-1.1-subset software GL, the `tinygl` submodule) and replace **only its
software rasterizer** with our engine. We do *not* hand-write a GL.

```
app (gears, GLQuake) → TinyGL  ── transform · lighting · clipping (incl. near plane)   [SW, reused as-is]
                              └─ ZB_fillTriangle* / ZB_line / ZB_plot  → tgl_engine.cc  [OUR backend]
                                    → submit_triangle → … → rasterize.sv               [HW]
```

Why TinyGL over hand-rolling: it already implements the matrix stack, per-vertex
lighting, frustum + **near-plane clipping**, display lists, and the immediate-mode
state machine — the exact "miniGL driver" we'd otherwise write — and exposes a
clean screen-space-vertex seam (`ZBufferPoint`).

**This is the authentic Voodoo/miniGL split.** GLQuake on a 3dfx Voodoo ran T&L
on the CPU and only rasterization on the card; the Voodoo had no transform engine.
So "TinyGL does geometry in SW, our HW rasterizes" is not a compromise — it is the
correct architecture for the target era. Hardware T&L is a later GPU generation
and an explicitly out-of-scope future project (§9).

### What is reused vs. replaced

- **Reused, unmodified:** all of TinyGL except two object files — the geometry/
  state/clip/lighting pipeline, texture object management, display lists.
- **Replaced:** `ztriangle.o` (the `ZB_fillTriangle*` fills) and `zline.o`
  (`ZB_line`/`ZB_plot`). We provide those 10 symbols in `tgl_engine.cc`.
- **TinyGL: prefer no source edits** (keep it a clean, swappable library) — the
  swap is purely at link time: build TinyGL minus those two objects, link our
  symbols in their place. (Build TinyGL with clang and no `-fopenmp`/`-march=native`
  for portability; OpenMP lived mostly in the excluded rasterizer.) Edit TinyGL
  only if a workaround in the app or the seam is clearly worse.
- **Clear / present** are driven at the harness level (`hw_clear` / `hw_fb_read`);
  TinyGL's own software framebuffer (`zb->pbuf`/`zbuf`) is allocated but unused —
  a small waste we'll stub away on the FPGA build.

### Degrees of freedom: the application source is ours too

GLQuake is **not** a fixed external dependency — `~/code/sdlquake` is our working
copy and we will hack it freely. So the goal is **not** "implement a faithful GL
that GLQuake happens to run on." The goal is "get GLQuake's geometry and textures
to the seam," and we have three knobs, to be used in whatever combination is
*simplest per feature*:

1. **Reuse** TinyGL's pipeline (the default for transform/light/clip).
2. **Edit GLQuake source** — delete or rewrite code paths rather than build API to
   satisfy them.
3. **Add an engine feature** — only when it's the genuinely right place (e.g.
   perspective texturing, blend modes).

This collapses a lot of the apparent surface. We do **not** need to faithfully
support: multitexture/`SGIS`, the 8-bit shared-palette extension, vertex-array
`EXT`s, gamma, DGA mouse, X vidmode switching, `glDrawBuffer` ztrick tricks, etc.
Rather than carry null `PROC` stubs and rely on runtime guards, we just `#if 0` /
delete those paths in the GLQuake source and force the simple branch. A missing or
awkward GL call is a source edit in `gl_*.c`, not a new API obligation.

Rule of thumb: **build API only for what's on the hot path and genuinely GL-shaped
(triangles, textures, blend, depth). Everything else, hack around in the app.**

**Platform/windowing code is ripped out wholesale.** Anything tied to X11, GLX,
DGA, svgalib, VESA/vidmode, or any other host backend gets deleted and replaced
with the *single simplest thing that works for us* — one SDL window + our engine
present, period. We are not preserving portability across windowing systems; we
target exactly one path. The same goes for CD audio, networking we don't use, and
any `#ifdef` maze — collapse to the one branch we run.

### Seam data conventions (`ZBufferPoint` → engine)

- `zp.x,zp.y`: whole-pixel ints (divide+viewport already applied) → ×32 to sub-pixel;
  `zp.y` already top-down.
- `zp.z`: in `[0, 2^30]`, **near = large**; engine wants 24-bit **near = small** →
  `our_depth = (2^30 − zp.z) >> 6`.
- `zp.r/g/b`: `~v·0xfe0000` fixed point → 8-bit is the top byte.
- `zb->depth_test` → `z_enable`.

---

## 4. GL surface coverage

GLQuake calls **~69 `gl*`** entry points and includes `<GL/gl.h>`/`<GL/glu.h>`, but
makes **no live `glu*` calls** (see the GLU decision below) and uses GLX only for
window/context (replaced by our vid, §6). TinyGL exports **213** `gl*` symbols, so
most are covered. The gaps:

| Category | Symbols | Plan |
|---|---|---|
| Trivial-exact | `glOrtho` (matrix + `glMultMatrixf`) | implement |
| Wrappers | `glTexParameterf`→`…i`, `glTexEnvf`→`…i` | implement |
| No-ops (engine has no equivalent / fixed behavior) | `glDepthFunc`, `glDepthRange`, `glAlphaFunc`, `glFogf/fv/i`, `glTexSubImage2D` | stub |
| Extensions (multitexture / 8-bit palette / vertex arrays) | `glColorTableEXT`, `glSelectTextureSGIS`, `glMTexCoord2fSGIS`, `glArrayElementEXT`, `glColorPointerEXT`, `glTexCoordPointerEXT`, `glVertexPointerEXT`, the `qgl*`/`PROC` pointers | **delete the paths in the GLQuake source** (force `gl_mtexable`/`is8bit` false and `#if 0` the branches) rather than carry stubs |
| Win32-only | `glCreateContext`, `glMakeCurrent`, `glGetProcAddress`, … | excluded on Linux (`#ifdef _WIN32`) — not referenced |
| GLX | `glXChooseVisual`, `glX*` | replaced by the SDL vid (§6) — not compiled |

Consequences of the no-op stubs (acceptable for first-light; "viz can be very
wrong"): no alpha-test cutouts (grates/fences render solid), depth func fixed at
`<` rather than `LEQUAL`, no fog, lightmaps frozen at their first upload.

### GLU decision: none needed (the historically correct answer)

3dfx's **MiniGL never included GLU** — GLU is a separate utility library layered
on GL, not part of the GL subset MiniGL implemented. And GLQuake was written to
stay MiniGL-friendly: it has its **own** `MYgluPerspective` (`gl_rmain.c`, no GLU
for projection) and its **own** `GL_ResampleTexture`/`GL_MipMap`/`GL_Upload32`
(`gl_draw.c`, no GLU for textures). The only `glu*` calls in the source —
`gluBuild2DMipmaps`/`gluScaleImage` in `GL_Upload32` — sit inside an **`#if 0`**
block (dead code; the live `#else` uses id's own resampler).

**So our sdlquake makes zero live GLU calls.** (An earlier grep "found" GLU only
because it ignores `#if 0` and matched the `MY`-prefixed name.)

**Decision: no GLU — no shim, no `libGLU.a`.** The `#include <GL/glu.h>` only needs
a *header that parses* (it links no symbols). The system `/usr/include/GL/glu.h`
suffices (it pulls in TinyGL's `gl.h` via our `-I` order and declares prototypes we
never call), or a near-empty stub header if we want zero system coupling. This
both matches the MiniGL era and is the simplest option. *(Note: this supersedes
the GLU shim sketched during exploration — `tgl_quake_compat.c` keeps only the
non-GLU stubs, §4.)*

For reference, had we wanted real GLU: the shared `libGLU.so` is `NEEDED`-linked to
the real `libOpenGL.so.0` (would shadow TinyGL — unusable), and the static
`libGLU.a`, while linkable against TinyGL, drags in references to ~18 GL functions
TinyGL lacks (NURBS/evaluator/quadric) that nothing calls. Both are strictly worse
than not linking GLU at all.

---

## 5. Texturing & blending — the work that makes viz *correct*

First light is untextured (white-modulated Gouraud) so the build runs. Making it
look right is the next engine-side work, all in `tgl_engine.cc`:

1. **Textured fills.** `ZB_fillTriangleMappingPerspective` currently routes
   untextured. Wire it: on `glTexImage2D`/`ZB_setTexture`, upload TinyGL's bound
   texture to the engine's texture memory; per triangle feed perspective `u/v` +
   `1/w`. Open question: TinyGL stores affine `s,t` in `ZBufferPoint` and recovers
   perspective via the zbuffer-z as denominator — decide whether to (a) recompute
   true `1/w` at the `GLVertex` level (needs intercepting one level up, where `w`
   is available) or (b) mirror TinyGL's z-as-denominator scheme into our planes.
2. **Texture object management.** Map TinyGL texture handles ↔ engine texture
   slots; handle `glBindTexture`, re-upload on rebind, mip levels. (Engine texture
   memory is finite — Quake has many textures → a residency/LRU question, ties into
   the bandwidth strategy in [next-steps](next-steps.html).)
3. **Blend modes.** Map `zb->enable_blend` + `sfactor/dfactor` → engine
   `blend_mode` (src-over / additive). Quake needs src-over (HUD, water) and
   additive-ish (particles, lightning).
4. **Alpha test** (optional, later): grates/fences. Engine has none today; could
   add a texel-alpha kill, or accept solid.
5. **Lightmaps** (later): Quake surfaces are `texture × lightmap`. The faithful-GL
   way needs a multiply blend / second texture pass + `glTexSubImage2D` updates.
   But with source freedom there's a much cheaper route: **pre-multiply on the CPU**
   in `gl_rsurf.c` — fold the lightmap into the surface texels (or into per-vertex
   color) when building the draw, so a single modulated pass through the engine
   already looks lit. No HW multitexture/multiply pass, no `glTexSubImage2D`. This
   is the preferred first cut for "looks like Quake"; a HW lightmap pass is a later
   optimization only if the CPU cost matters.

---

## 6. The vid layer

Replaces GLQuake's `gl_vidlinuxglx.c` (X11 + GLX, ~1000 lines) with an SDL +
TinyGL + engine version (`gl_vid_tinygl.c`). It owns the window/context lifecycle
and must provide the exact symbol surface the rest of GLQuake links against:

- **Functions:** `VID_Init`, `VID_Shutdown`, `VID_SetPalette`, `VID_ShiftPalette`,
  `VID_Is8bit`, `GL_Init`, `GL_BeginRendering`, `GL_EndRendering`,
  `D_BeginDirectRect`, `D_EndDirectRect`, `Sys_SendKeyEvents`,
  `CheckMultiTextureExtensions`.
- **Globals:** `d_8to16table`/`d_8to24table`/`d_15to8table`, `vid_mode`/`gl_ztrick`
  cvars, `texture_mode`, `texture_extension_number`, `gldepthmin`/`gldepthmax`,
  `gl_vendor/renderer/version/extensions`, `is8bit`, `isPermedia`, `gl_mtexable`,
  the `qgl*`/EXT `PROC` pointers.
- **Lifecycle:** `VID_Init` → `ZB_open` + `glInit` (TinyGL context) + `hw_open` +
  `tgl_set_device` + load white texture + create SDL window/renderer/texture, fill
  `vid_t` (conwidth/conheight/width/height/aspect/numpages/colormap/fullbright).
  `GL_BeginRendering` returns `0,0,w,h`. `GL_EndRendering` → `hw_fb_read` → SDL
  present. `Sys_SendKeyEvents` → SDL event pump → `Key_Event` (+ mouse later).

Per §3's degrees of freedom, we **shrink this surface by editing the source**, not
by reproducing all of `gl_vidlinuxglx.c`: rip out gamma, DGA mouse, X vidmode
switching, and the 8-bit palette path from the GLQuake side so the vid layer only
has to provide the minimal window/context/present/input set above. Easiest is to
start from the existing `vid_sdl.c` (the project already does SDL) rather than the
GLX file.

---

## 7. GLQuake build (cross-repo)

Source + data live in `~/code/sdlquake` (full `gl_*.c` renderer; `id1/pak0.pak` +
`pak1.pak` present, so real maps load). It is currently configured for the
*software* renderer; we build the **GL object set** instead.

- **Drop** (software renderer): `d_*`, `r_*`, `draw`, `screen`, `model`, `vid_sdl`,
  `nonintel`.
- **Add** (GL renderer): `gl_draw gl_mesh gl_model gl_refrag gl_rlight gl_rmain
  gl_rmisc gl_rsurf gl_screen gl_warp` + our `gl_vid_tinygl`.
- **Keep** (shared engine): `cl_* host* cmd common console crc cvar keys mathlib
  menu net_* pr_* sbar snd_* sv_* sys_sdl view wad world zone cd_sdl chase`.
- **Link with:** `tgl_engine.o` + `tgl_quake_compat.o` (non-GLU GL stubs) + TinyGL
  objects (minus `ztriangle.o`/`zline.o`) + the engine (`gfx setup
  hw_rast_verilated verilated obj_dir_demo`). No GLU.
- **Include order:** `-Itinygl/include` first so `<GL/gl.h>` resolves to TinyGL;
  `<GL/glu.h>` resolves to the system header (or a stub) — it links nothing.
- **Vendoring (decided):** **fork sdlquake.** This effort hacks the source heavily
  (rip out X11/GLX/DGA/vidmode, strip extensions, pre-multiply lightmaps, swap the
  vid layer), so it gets its own fork we own and commit to, vendored into this repo
  as a submodule. The fork carries the local files (`va2pa.c`, `reciplogger.cc`).
  Pak data (`id1/pak0.pak`, `pak1.pak`) stays out of git (size/licensing) — kept
  locally and pointed at by `-basedir`.

Runtime expectations: cycle-accurate sim is slow (seconds per frame; a full Quake
scene is heavy) — fine for correctness, not playability. First-light success =
"it boots to the menu / renders a frame," not "it's playable."

---

## 8. Expected other consumers

The miniGL/seam is not GLQuake-specific. Expected users, in rough order:

- **TinyGL demos** — `gears` (done); TinyGL ships more (`Raw_Demos`,
  `SDL_Examples`) that exercise texturing/blending/text and make good incremental
  test cases before Quake.
- **GLQuake** — the north star; defines the subset and the texturing/lightmap work.
- **Other small OpenGL-1.x apps** — once textured perspective + blending land, the
  path generalizes to other immediate-mode GL programs that stay within TinyGL's
  subset.
- **The existing IRIS GL and OBJ front-ends** — already converged on the seam;
  this doc doesn't change them, but engine improvements (textured-fill fidelity,
  blend modes) benefit all three.

Non-goals (for this effort): GL conformance, shaders/programmable pipeline,
hardware T&L, stencil. We target the GLQuake-class fixed-function subset only.

---

## 9. FPGA transfer story

The point of doing this on x86 first is that **the software plumbing transfers**.
On the Ultra96/ZU3EG:

- **TinyGL + `tgl_engine` + the vid layer** → run on the **ARM (A53)**; portable C,
  cross-compiles directly. This becomes the on-board miniGL driver.
- **`hw_rast_verilated` backend** → swapped for an **AXI backend** implementing the
  same `hw_rast.h` ABI over the PL (the `submit_triangle`/`hw_tri` contract is the
  stable boundary).
- **`obj_dir_demo` (Verilator)** → the real **PL bitstream**.
- T&L stays on the A53 (the Voodoo split) — a P100-class CPU ran Quake's geometry;
  the A53 is comfortably enough. The PS→PL handoff is a packed setup buffer over
  DMA, not register pokes (see [next-steps](next-steps.html) bandwidth notes).
- Resolution couples to bandwidth: on-chip FB+Z fits ~320×240 (authentically
  Quake-era); higher res forces FB to DRAM or tiling.

---

## 10. Open decisions

1. **Perspective texturing approach** (§5.1): recompute true `1/w` at `GLVertex`
   level vs. mirror TinyGL's zbuffer-z denominator. (Affects whether we intercept
   at `ZB_*` or one level up in `clip.c`.)
2. **Texture residency** — Quake's texture set vs. finite engine texture memory:
   LRU/streaming, ties to the bandwidth plan.
3. ~~sdlquake vendoring~~ **(decided: fork it, vendor as a submodule; see §7).**
4. **GLU** — shim accepted; revisit only if a consumer needs NURBS/quadrics.
5. **Lightmaps** — multiply second pass vs. multitexture; when to tackle.

---

## 11. Phasing

1. **First light** — build GLQuake against TinyGL + engine, boot to a rendered
   frame (untextured, no alpha test/fog/lightmap). Includes **hacking the GLQuake
   source** to strip the paths we don't support (multitexture, 8-bit palette,
   gamma, DGA, vidmode) and force the simple branches, rather than building API for
   them. Validates the whole stack.
2. **Textured perspective fills** — wire `tgl_engine` texturing; world geometry
   gets its textures.
3. **Blending** — src-over + additive mapped through; HUD, water, particles.
4. **Lightmaps** — the "looks like Quake" pass.
5. **FPGA backend** — swap the Verilator backend for AXI; run on the board.

Code starts at phase 1 once this design is agreed.
