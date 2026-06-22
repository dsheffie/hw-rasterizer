// Fixed-point reciprocal: approximates 1/d for a positive integer d via range
// normalization, an 8-bit-indexed seed ROM, and one Newton-Raphson iteration
// (y' = y*(2 - d*y)).  Bit-exact software model: recip_fixed() in top.cc.
//
// Output is a normalized (mantissa, exponent) pair:
//     1/d ~= y * 2^-(16 + e)
// so a downstream multiply recovers an attribute as (attr * y) >> (16 + e).
//
// 4-stage pipeline (latency L=4, throughput 1/cycle), valid flowing with the
// data.  Stage split keeps the two dependent multiplies in separate stages:
//   S1 clz + normalize | S2 seed ROM | S3 M*y0 + (2-M*y0) | S4 y0*tm + shift
module recip(input  logic        clk,
	     input  logic        rst,
	     input  logic        en,       // clock-enable: freeze the pipe when low
	     input  logic        valid_in,
	     input  logic [31:0]  d,
	     output logic         valid_out,
	     output logic         busy,     // any stage holds a valid fragment
	     output logic [17:0]  y,        // Q16 reciprocal of the mantissa
	     output logic [4:0]   e);       // MSB position (d in [2^e, 2^(e+1)))

   // seed ROM: Q16 reciprocal of a mantissa in [1,2), indexed by its top 8
   // fractional bits.  entry i = round(65536 / (1 + (i+0.5)/256)).
   // embedded directly (was $readmemh("recip_seed.hex")) so the Verilated model
   // needs no external file at runtime -- it runs from any working directory.
   logic [16:0] seed_rom [0:255];
   initial seed_rom = '{
      17'h0ff80, 17'h0fe82, 17'h0fd86, 17'h0fc8c, 17'h0fb94, 17'h0fa9e, 17'h0f9a9, 17'h0f8b7,
      17'h0f7c6, 17'h0f6d7, 17'h0f5ea, 17'h0f4ff, 17'h0f415, 17'h0f32d, 17'h0f247, 17'h0f163,
      17'h0f080, 17'h0ef9f, 17'h0eebf, 17'h0ede1, 17'h0ed05, 17'h0ec2a, 17'h0eb51, 17'h0ea7a,
      17'h0e9a4, 17'h0e8cf, 17'h0e7fc, 17'h0e72b, 17'h0e65b, 17'h0e58c, 17'h0e4bf, 17'h0e3f4,
      17'h0e329, 17'h0e260, 17'h0e199, 17'h0e0d3, 17'h0e00e, 17'h0df4b, 17'h0de88, 17'h0ddc8,
      17'h0dd08, 17'h0dc4a, 17'h0db8d, 17'h0dad1, 17'h0da17, 17'h0d95e, 17'h0d8a6, 17'h0d7ef,
      17'h0d73a, 17'h0d685, 17'h0d5d2, 17'h0d520, 17'h0d46f, 17'h0d3bf, 17'h0d311, 17'h0d263,
      17'h0d1b7, 17'h0d10c, 17'h0d062, 17'h0cfb9, 17'h0cf11, 17'h0ce6a, 17'h0cdc4, 17'h0cd1f,
      17'h0cc7b, 17'h0cbd8, 17'h0cb36, 17'h0ca96, 17'h0c9f6, 17'h0c957, 17'h0c8b9, 17'h0c81c,
      17'h0c780, 17'h0c6e5, 17'h0c64b, 17'h0c5b2, 17'h0c51a, 17'h0c482, 17'h0c3ec, 17'h0c357,
      17'h0c2c2, 17'h0c22e, 17'h0c19b, 17'h0c109, 17'h0c078, 17'h0bfe8, 17'h0bf59, 17'h0beca,
      17'h0be3c, 17'h0bdaf, 17'h0bd23, 17'h0bc98, 17'h0bc0d, 17'h0bb83, 17'h0bafb, 17'h0ba72,
      17'h0b9eb, 17'h0b964, 17'h0b8de, 17'h0b859, 17'h0b7d5, 17'h0b751, 17'h0b6ce, 17'h0b64c,
      17'h0b5cb, 17'h0b54a, 17'h0b4ca, 17'h0b44b, 17'h0b3cc, 17'h0b34e, 17'h0b2d1, 17'h0b254,
      17'h0b1d8, 17'h0b15d, 17'h0b0e3, 17'h0b069, 17'h0aff0, 17'h0af77, 17'h0aeff, 17'h0ae88,
      17'h0ae11, 17'h0ad9b, 17'h0ad26, 17'h0acb1, 17'h0ac3d, 17'h0abc9, 17'h0ab56, 17'h0aae4,
      17'h0aa72, 17'h0aa01, 17'h0a990, 17'h0a920, 17'h0a8b1, 17'h0a842, 17'h0a7d3, 17'h0a766,
      17'h0a6f8, 17'h0a68c, 17'h0a620, 17'h0a5b4, 17'h0a549, 17'h0a4df, 17'h0a475, 17'h0a40c,
      17'h0a3a3, 17'h0a33a, 17'h0a2d3, 17'h0a26b, 17'h0a204, 17'h0a19e, 17'h0a138, 17'h0a0d3,
      17'h0a06e, 17'h0a00a, 17'h09fa6, 17'h09f43, 17'h09ee0, 17'h09e7e, 17'h09e1c, 17'h09dba,
      17'h09d59, 17'h09cf9, 17'h09c99, 17'h09c39, 17'h09bda, 17'h09b7c, 17'h09b1d, 17'h09ac0,
      17'h09a62, 17'h09a05, 17'h099a9, 17'h0994d, 17'h098f1, 17'h09896, 17'h0983b, 17'h097e1,
      17'h09787, 17'h0972e, 17'h096d5, 17'h0967c, 17'h09624, 17'h095cc, 17'h09574, 17'h0951d,
      17'h094c7, 17'h09470, 17'h0941b, 17'h093c5, 17'h09370, 17'h0931b, 17'h092c7, 17'h09273,
      17'h0921f, 17'h091cc, 17'h09179, 17'h09127, 17'h090d5, 17'h09083, 17'h09032, 17'h08fe1,
      17'h08f90, 17'h08f40, 17'h08ef0, 17'h08ea0, 17'h08e51, 17'h08e02, 17'h08db3, 17'h08d65,
      17'h08d17, 17'h08cc9, 17'h08c7c, 17'h08c2f, 17'h08be2, 17'h08b96, 17'h08b4a, 17'h08aff,
      17'h08ab3, 17'h08a68, 17'h08a1e, 17'h089d3, 17'h08989, 17'h08940, 17'h088f6, 17'h088ad,
      17'h08864, 17'h0881c, 17'h087d3, 17'h0878c, 17'h08744, 17'h086fd, 17'h086b6, 17'h0866f,
      17'h08628, 17'h085e2, 17'h0859c, 17'h08557, 17'h08511, 17'h084cc, 17'h08488, 17'h08443,
      17'h083ff, 17'h083bb, 17'h08377, 17'h08334, 17'h082f1, 17'h082ae, 17'h0826b, 17'h08229,
      17'h081e7, 17'h081a5, 17'h08164, 17'h08123, 17'h080e2, 17'h080a1, 17'h08060, 17'h08020
   };

   // ---- stage 1 comb: clz -> exponent, normalize to Q16 mantissa ----
   logic [4:0]  t_e;
   logic [16:0] t_M;
   always_comb begin
      t_e = 5'd0;
      for(int i = 0; i < 32; i++) if(d[i]) t_e = i[4:0];
      if(t_e >= 5'd16) t_M = 17'(d >> (t_e - 5'd16));   // value always fits 17 bits
      else             t_M = 17'(d << (5'd16 - t_e));
   end
   logic [16:0] r1_M;
   logic [4:0]  r1_e;
   logic        r1_v;

   // ---- stage 2 comb: seed ROM lookup (top 8 fractional bits = M[15:8]) ----
   wire  [16:0] t_y0 = seed_rom[r1_M[15:8]];
   logic [16:0] r2_y0, r2_M;
   logic [4:0]  r2_e;
   logic        r2_v;

   // ---- stage 3 comb: first multiply + (2 - d*y) ----
   wire  [33:0] t_my = r2_M * r2_y0;             // < 2^33
   wire  [33:0] t_tm = 34'h2_0000_0000 - t_my;   // 2 - d*y, Q32
   logic [33:0] r3_tm;
   logic [16:0] r3_y0;
   logic [4:0]  r3_e;
   logic        r3_v;

   // ---- stage 4 comb: second multiply + final shift ----
   wire  [50:0] t_prod = r3_y0 * r3_tm;          // < 2^50
   logic [17:0] r4_y;
   logic [4:0]  r4_e;
   logic        r4_v;

   always_ff @(posedge clk) begin
      if(rst) begin
	 r1_v <= 1'b0; r2_v <= 1'b0; r3_v <= 1'b0; r4_v <= 1'b0;
      end
      else if(en) begin
	 r1_v <= valid_in; r2_v <= r1_v; r3_v <= r2_v; r4_v <= r3_v;
      end
      if(en) begin
	 r1_M  <= t_M;           r1_e <= t_e;
	 r2_y0 <= t_y0;          r2_e <= r1_e;  r2_M <= r1_M;
	 r3_tm <= t_tm;          r3_e <= r2_e;  r3_y0 <= r2_y0;
	 r4_y  <= t_prod[49:32]; r4_e <= r3_e;
      end
   end

   assign y         = r4_y;
   assign e         = r4_e;
   assign valid_out = r4_v;
   assign busy      = r1_v | r2_v | r3_v | r4_v;

endmodule // recip
