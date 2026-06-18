// Standalone testbench for recip.sv.  Sweeps d and checks the RTL output
// (mantissa y, exponent e) bit-for-bit against the integer golden model
// (which mirrors recip_fixed() in top.cc), and reports the worst-case
// relative error of the reconstructed reciprocal vs. the true 1/d.
#include <cstdint>
#include <cstdio>
#include <cmath>
#include "Vrecip.h"
#include "verilated.h"

static uint32_t seed[256];
static void init_seed() {
  for(int i = 0; i < 256; i++) {
    double m = 1.0 + (i + 0.5) / 256.0;
    seed[i] = (uint32_t)llround(65536.0 / m);
  }
}

// integer golden, one Newton step -- identical math to recip.sv
static void golden(uint32_t d, uint32_t &gy, uint32_t &ge) {
  int e = 31 - __builtin_clz(d);
  uint64_t M = (e >= 16) ? (uint64_t)(d >> (e - 16))
                         : ((uint64_t)d << (16 - e));
  uint64_t Y = seed[(M >> 8) & 0xff];
  uint64_t MY = M * Y;
  Y = (Y * (((uint64_t)1 << 33) - MY)) >> 32;
  gy = (uint32_t)Y;
  ge = (uint32_t)e;
}

int main(int argc, char **argv) {
  Verilated::commandArgs(argc, argv);
  init_seed();
  Vrecip *dut = new Vrecip;

  const uint32_t DMAX = 1u << 20;
  uint64_t tested = 0, mism = 0;
  double maxrel = 0;
  for(uint32_t d = 1; d <= DMAX; d++) {
    dut->d = d;
    dut->eval();
    uint32_t gy, ge;
    golden(d, gy, ge);
    if(dut->y != gy || dut->e != ge) {
      if(mism < 10)
        printf("MISMATCH d=%u  rtl(y=%u,e=%u)  gold(y=%u,e=%u)\n",
               d, dut->y, dut->e, gy, ge);
      mism++;
    }
    double r  = (double)dut->y / (double)((uint64_t)1 << (16 + dut->e));
    double rt = 1.0 / (double)d;
    double rel = std::fabs(r - rt) / rt;
    if(rel > maxrel) maxrel = rel;
    tested++;
  }

  printf("recip_tb: tested %llu values [1..%u], %llu mismatches, max rel err %.3g\n",
         (unsigned long long)tested, DMAX, (unsigned long long)mism, maxrel);
  delete dut;
  return mism ? 1 : 0;
}
