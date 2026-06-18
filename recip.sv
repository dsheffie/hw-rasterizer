// Fixed-point reciprocal: approximates 1/d for a positive integer d via range
// normalization, an 8-bit-indexed seed ROM, and one Newton-Raphson iteration
// (y' = y*(2 - d*y)).  Bit-exact software model: recip_fixed() in top.cc.
//
// Output is a normalized (mantissa, exponent) pair:
//     1/d ~= y * 2^-(16 + e)
// so a downstream multiply recovers an attribute as (attr * y) >> (16 + e).
//
// Combinational for now; pipelined next once the latency target is known.
module recip(input  logic [31:0] d,
	     output logic [17:0] y,     // Q16 reciprocal of the mantissa
	     output logic [4:0]  e);    // MSB position of d (d in [2^e, 2^(e+1)))

   // seed ROM: Q16 reciprocal of a mantissa in [1,2), indexed by its top 8
   // fractional bits.  entry i = round(65536 / (1 + (i+0.5)/256)).
   logic [16:0] seed_rom [0:255];
   initial $readmemh("recip_seed.hex", seed_rom);

   // 1) find MSB of d -> exponent e (d assumed > 0)
   always_comb begin
      e = 5'd0;
      for(int i = 0; i < 32; i++)
	if(d[i]) e = i[4:0];
   end

   // 2) normalize d to a Q16 mantissa M in [2^16, 2^17)
   logic [16:0] M;
   always_comb begin
      if(e >= 5'd16) M = 17'(d >> (e - 5'd16));   // value always fits in 17 bits
      else           M = 17'(d << (5'd16 - e));
   end

   // 3) seed reciprocal of the mantissa (top 8 fractional bits = M[15:8])
   wire [16:0] y0 = seed_rom[M[15:8]];

   // 4) one Newton step: y = (y0 * (2^33 - M*y0)) >> 32
   wire [33:0] my   = M * y0;                  // < 2^33
   wire [33:0] tm   = 34'h2_0000_0000 - my;    // 2 - d*y, Q32
   wire [50:0] prod = y0 * tm;                 // < 2^50
   assign y = prod[49:32];                     // >> 32, back to ~Q16

endmodule // recip
