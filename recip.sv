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
   logic [16:0] seed_rom [0:255];
   initial $readmemh("recip_seed.hex", seed_rom);

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
