# Tile-Based Rendering — Target Architecture & the CRIME Tradeoff

Notes on moving the rasterizer to a tile-based design with on-chip local memories
for depth and color, and how that bet differs from the SGI CRIME design
(`docs/crime-rendering-engine-notes.md`).

## Why tile-based: it closes the back-end bandwidth wall

The per-fragment depth-test and blend are read-modify-writes. In an immediate-mode
design against a full-frame buffer in shared SDRAM, those RMWs (multiplied by
overdraw) are the single biggest bandwidth consumer and the thing that makes
1 pixel/clock hard.

Put the depth + color buffers for **one tile** in on-chip SRAM and those RMWs
become single-cycle local accesses. That is what makes 1 pixel/clock real: the
deep pipe hides texture latency, the tile SRAM removes the framebuffer round-trip.

### What it buys

- **Depth → zero external traffic.** Allocate the tile Z buffer in SRAM, clear at
  tile start, use it, discard at tile end (no readback needed in the common case).
  The full-frame Z bandwidth — the biggest consumer under overdraw — disappears.
- **Color written once per tile.** Blend locally, stream the resolved tile out at
  flush. External write traffic ≈ one frame-size, independent of overdraw.
- **Small SRAM.** 32×32 tile × (4 B depth+stencil + 4 B RGBA) = 8 KB; 16×16 = 2 KB.
- **Free extras:** local blending is cheap; MSAA can supersample inside the tile
  SRAM and resolve on flush (AA bandwidth stays on-chip); sets up for per-tile
  deferred hidden-surface removal (PowerVR-style) if desired.

### What it costs

- **Binning / sort-middle.** Must know which triangles touch a tile before
  rasterizing it. Frame splits into two phases: (1) transform + setup all geometry
  and bin each triangle into per-tile lists (the "parameter buffer"); (2) per tile,
  replay its list against local buffers. Adds a full-scene geometry buffer and
  latency.
- **Parameter-buffer overflow.** If the bin buffer fills mid-frame, flush a partial
  render and resume — the classic TBR wart.
- **Per-tile redundancy.** Big triangles get re-setup in every tile they overlap;
  many tiny triangles pay binning overhead. Fill/drain is per-tile (amortizes far
  better than per-triangle).
- **Texture bandwidth is NOT solved.** Tiling fixes the framebuffer side; texels
  still come from SDRAM, so the texture cache is still required. (Tile locality does
  help texture-cache hit rates.)

## The CRIME tradeoff: two opposite bets

CRIME shares one memory pool across CPU, graphics, video, and audio (UMA), with the
framebuffer RMW'd per fragment in system SDRAM. Through a "maximize 3D throughput"
lens this looks wrong, but 3D throughput was not the O2's objective function. UMA
was the machine's defining bet, and a deliberate one — SGI's high end
(InfiniteReality/Onyx) used the dedicated-memory monster design; the O2 was a
different point for a media/content-creation, cost-sensitive market.

What UMA buys, justifying the contention:

- **Zero-copy media interop (the real reason).** The O2's signature capability was
  real-time video-as-texture. With UMA the rendering engine textures directly from
  a frame the video hardware just wrote — no DMA copy from system RAM to graphics
  RAM. The other memory clients are producing the data 3D consumes.
- **Allocation flexibility.** No fixed VRAM/system partition; a giant texture, a
  huge framebuffer, or all-system can each use the entire pool.
- **Cost.** Commodity SDRAM, one memory system, no expensive dual-ported VRAM. The
  O2 was SGI's low end.
- **Adequate for the era.** ~2 GB/s on a 256-bit bus in 1996 was genuinely wide;
  the engine ran synchronous to memory at 66 MHz (no rate mismatch). Fine for 1996
  workstation resolutions and fill rates.

It also coheres with the rest of the design: CRIME's **virtual framebuffer**
(scattered TLB tiles in system memory) is a UMA *feature* — a windowing workstation
wants to allocate many framebuffers dynamically, anywhere, per-window. Immediate-
mode-into-shared-memory gives that. And in 1996 an OpenGL workstation needed clean
immediate-mode semantics (glReadPixels, mid-render state), territory where early
tile-based deferred rendering had real friction — so immediate mode was the
compatible, conservative choice.

Honest downside: it did bite them. The O2 was contention-bound under texture-heavy
3D, and UMA latency hurt CPU performance too (shared arbiter and pool).

### Summary

| | CRIME (O2) | Tile-based target |
|---|---|---|
| Memory | Shared UMA, framebuffer in SDRAM | On-chip SRAM per tile, write out once |
| Optimizes for | Media interop, flexibility, cost | 3D throughput, 1 pixel/clock |
| "Tile" means | SDRAM TLB pages (memory layout) | Render-to-on-chip-SRAM |
| Framebuffer/Z BW | Per-fragment RMW to shared pool | Local; ~one frame-size out |
| Texture BW | Shared pool | Still needs a texture cache |
| Mode | Immediate | Sort-middle (binning + parameter buffer) |

Note the naming collision: CRIME's "tiles" are 64 KB SDRAM pages behind the TLB
(`TLB.fbA/B/C`, `TLB.tex`) — a memory-*layout* tiling for locality. The tile-based
target here is **render-to-on-chip-tile** (the PowerVR, 1996, model). Same word,
different architecture; this is a deliberate step *past* CRIME, not a CRIME feature.

## For this project

This is a simplification of the sim's memory model, not just a feature add:

- **`top.cc`** gains a binning pass (per tile, collect triangles whose bbox
  overlaps) and a tile loop. Today it is immediate-mode: each triangle →
  `rasterize.sv` → software Z-test against a full-frame buffer.
- **Z and color buffers become small per-tile arrays** (model as block RAM,
  single-cycle) instead of the full-frame software buffer.
- **`rasterize.sv`** bounding-box walk gets clamped to the tile.
- The memory model needed for the 1-ppc study gets cleaner: local depth/color are
  single-cycle, so the only external traffic worth modeling is texture fetch (→
  cache), bin-list reads, and tile writeout.
