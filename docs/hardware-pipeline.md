---
title: The hardware pipeline
nav_order: 8
---

# The hardware pipeline — setup vs. iterate, and keeping it fed

The previous pages each introduced an [iterated linear function](index.html) or a
memory. This page is about how they're wired into a machine that produces one
fragment per cycle.

## Two halves: host setup, PL iteration

- The **host** (ARM core) does per-triangle **setup**: the cross products
  (`setup.cc`'s `cross`), the Cramer's-rule divides that produce every gradient
  (`setup_triangle`, `setup_attr`), the per-vertex lighting, and the LOD. This is
  a few dozen flops per triangle and leans on the CPU's floating point.
- The **PL** (FPGA fabric) does per-pixel **iteration**: adders for the edge
  functions, depth, `u/w·t/w·1/w`, and color; one reciprocal; the texture fetch +
  filter; the depth test; the color write. One fragment per cycle.

This split is why the inner loop is cheap: all the divides happen once per
triangle on the CPU; the hardware only adds, compares, and does a single
reciprocal.

## The fragment datapath

![Rasterizer datapath block diagram](img/datapath.svg)

*Block diagram of `rasterize.sv` + `recip.sv`. Dashed boxes are the three
domains (host setup, the per-pixel scan/steppers, the texture unit); cylinders
are on-chip memories. Solid arrows are the main fragment flow; dashed arrows are
the side-band values that ride alongside the reciprocal/texture latency. The
source is [`docs/img/datapath.dot`](img/datapath.dot) — regenerate with
`dot -Tsvg datapath.dot -o datapath.svg`.*

A covered fragment flows through, roughly:

```
scan/steppers → reciprocal (pipelined) → ×w (recover u,v)
              → texture fetch (4-bank bilinear, mip level)
              → depth test (read/compare/write Z BRAM)
              → modulate (texel × Gouraud color)
              → color framebuffer write
```

The **reciprocal is pipelined** — several cycles of latency, but one result per
cycle, so it costs latency without costing throughput. Because of that latency, a
**side-band** carries each fragment's address, depth, and interpolated color
*alongside* the reciprocal/texture pipeline, so they re-align at the stage that
needs them (the texture read and the depth/color write are issued at the matching
pipeline stage — the same trick used to hide the BRAM read latency in the depth
test).

## Backpressure

Most of the pipeline is fixed-latency and never stalls. The one place that *can*
stall is the path to off-chip texture memory (a cache miss), which has variable
latency. The design handles this with a **FIFO + credit-based flow control**: the
producer only issues work when it holds a credit for a downstream slot, and
credits are returned as the consumer drains. The key property is that
backpressure becomes a **local counter decision** rather than a long
combinational "is everything downstream ready this cycle" signal — which matters
for timing, and decouples the rasterizer from the memory system's jitter. (See
also the planning notes on the credit scheme.)

## On-chip everything — the bandwidth decision

The framebuffer and depth buffer live in **on-chip BRAM**, not DRAM. That keeps
the highest-frequency traffic — the per-fragment depth read-modify-write and the
color write, which happen for every fragment including overdraw — entirely
on-chip. The scarce off-chip bandwidth is then spent only on *cacheable* texture
reads.

This caps resolution to what BRAM holds (~256×256 on the target part —
conveniently, authentic Quake-era resolution). Going larger means either a
DRAM framebuffer (bringing that RMW traffic back onto the bus) or **tiling**:
keep one screen tile's color+depth on-chip, render it, then stream the finished
tile out as a burst — converting random RMW into coherent burst writes. Tiling is
the documented path to higher resolution.

Next: [Toward miniGL](toward-minigl.html).
