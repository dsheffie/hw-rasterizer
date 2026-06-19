
module rasterize(clk, rst, go,
		 v0_x, v0_y,
		 v1_x, v1_y,
		 v2_x, v2_y,
		 w0,w1,w2,
		 dzdx, dzdy, z_start,
		 dswdx, dswdy, sw_start,
		 dtwdx, dtwdy, tw_start,
		 diwdx, diwdy, iw_start,
		 dcrdx, dcrdy, cr_start,
		 dcgdx, dcgdy, cg_start,
		 dcbdx, dcbdy, cb_start,
		 tex_we, tex_wbank, tex_waddr, tex_wdata,
		 x_dim, y_dim,
		 clear,
		 fb_raddr, fb_rdata,
		 clearing,
		 done) ;
   // Depth interpolation fixed-point format: DEPTH_INT_BITS.DEPTH_FRAC_BITS
   // (signed).  Must match DEPTH_FRAC_BITS in setup.h.
   localparam DEPTH_FRAC_BITS = 12;
   localparam DEPTH_INT_BITS  = 25;
   localparam DEPTH_W         = DEPTH_INT_BITS + DEPTH_FRAC_BITS;

   // Perspective-correct texture attributes: interpolated linearly in screen
   // space exactly like depth (same stepper shape).  Fixed-point per the CRIME
   // model: u/w and t/w are 36.12, 1/w is 18.12.  The reciprocal + texel
   // lookup are done downstream (software for now), so the full fixed-point
   // value is carried out per fragment.
   localparam ATTR_FRAC_BITS = 12;
   localparam SW_W = 48;   // 36.12 for u/w and t/w
   localparam IW_W = 30;   // 18.12 for 1/w

   // Gouraud color attributes: affine (no perspective divide), same stepper
   // shape as depth.  Signed fixed-point, COL_FRAC fractional bits.
   localparam COL_FRAC = 12;
   localparam COL_W    = 24;

   // texture memory (BRAM): TEX_W x TEX_W RGB, power-of-two so REPEAT wrap is
   // just a low-bit mask.  uv is Q16, so the texel coord is uv >> (16-TEX_LW).
   localparam TEX_LW = 7;            // log2 texture dimension (128x128)
   localparam TEX_W  = 1 << TEX_LW;
   localparam TEX_N  = TEX_W * TEX_W;
   localparam TEX_AW = 2 * TEX_LW;
   // bilinear: 4 banks split by (x&1,y&1) for a 1-cycle 2x2 fetch
   localparam TEX_FB  = 16 - TEX_LW; // fractional bits of the texel coordinate
   // mip chain stacked in each bank: a uniform (TEX_W/2)^2 slot per level, so a
   // bank address is just {level, row>>1, col>>1}.
   localparam MIP_LEVELS = TEX_LW + 1;             // 128..1 -> 8 levels
   localparam TEXB_SLOT  = (TEX_W/2) * (TEX_W/2);  // per-level per-bank (64x64)
   localparam TEXB_N     = MIP_LEVELS * TEXB_SLOT;
   localparam TEXB_AW    = $clog2(TEXB_N);

   // on-chip color + depth buffer geometry.  SCREEN_RES is a build-time knob
   // (default 256 fits the ZU3EG BRAM; `make RES=512` bumps it for sim).
`ifndef SCREEN_RES
 `define SCREEN_RES 256
`endif
   localparam SCREEN_W = `SCREEN_RES;
   localparam SCREEN_H = `SCREEN_RES;
   localparam FB_N  = SCREEN_W * SCREEN_H;
   localparam FB_AW = $clog2(FB_N);
   localparam Z_W   = 16;

   input logic clk;
   input logic rst;
   input logic go;
   input logic [31:0] v0_x;
   input logic [31:0] v0_y;
   
   input logic [31:0] v1_x;
   input logic [31:0] v1_y;
   
   input logic [31:0] v2_x;
   input logic [31:0] v2_y;
   
   input logic [31:0] w0;
   input logic [31:0] w1;
   input logic [31:0] w2;
   input logic [DEPTH_W-1:0] dzdx;
   input logic [DEPTH_W-1:0] dzdy;
   input logic [DEPTH_W-1:0] z_start;
   input logic [SW_W-1:0] dswdx;
   input logic [SW_W-1:0] dswdy;
   input logic [SW_W-1:0] sw_start;
   input logic [SW_W-1:0] dtwdx;
   input logic [SW_W-1:0] dtwdy;
   input logic [SW_W-1:0] tw_start;
   input logic [IW_W-1:0] diwdx;
   input logic [IW_W-1:0] diwdy;
   input logic [IW_W-1:0] iw_start;
   input logic [COL_W-1:0] dcrdx, dcrdy, cr_start;   // Gouraud R plane
   input logic [COL_W-1:0] dcgdx, dcgdy, cg_start;   // Gouraud G plane
   input logic [COL_W-1:0] dcbdx, dcbdy, cb_start;   // Gouraud B plane
   input logic 	      tex_we;       // texture-load write enable (host)
   input logic [1:0]  tex_wbank;    // texture-load bank (host computes)
   input logic [31:0] tex_waddr;    // texture-load bank address (incl. level)
   input logic [23:0] tex_wdata;    // texture-load texel (R,G,B)
   input logic [31:0] x_dim;
   input logic [31:0] y_dim;


   input logic 	      clear;      // pulse to reset the color + depth buffers
   input logic [31:0] fb_raddr;    // framebuffer read address (host scan-out)

   output logic [23:0] fb_rdata;   // framebuffer read data (registered)
   output logic        clearing;   // high while the buffers are being cleared
   output logic        done;
   
   typedef enum logic [4:0] {
			     IDLE,
			     COMPUTE_BB,
			     COMPUTE_XVEC,
			     COMPUTE_YVEC,
			     COMPUTE_START_ADDR,
			     COMPUTE_START_CROSS_V1V2_X,
			     COMPUTE_START_CROSS_V2V0_X,
			     COMPUTE_START_CROSS_V0V1_X,
			     COMPUTE_START_CROSS_V1V2_Y,
			     COMPUTE_START_CROSS_V2V0_Y,
			     COMPUTE_START_CROSS_V0V1_Y,
			     
			     RENDER,
			     DRAIN,
			     HANG
			     } state_t;
   
   state_t r_state, n_state;
   logic [31:0] r_v0_x, r_v0_y;
   logic [31:0] r_v1_x, r_v1_y;
   logic [31:0] r_v2_x, r_v2_y;
   logic 	t_save_tri;

   logic [31:0] n_x_min, r_x_min;
   logic [31:0] n_x_max, r_x_max;
   logic [31:0] n_y_min, r_y_min;
   logic [31:0] n_y_max, r_y_max;


   //vertex2d v2v1(v2.x-v1.x, v2.y-v1.y);
   //vertex2d v0v2(v0.x-v2.x, v0.y-v2.y);
   //vertex2d v1v0(v1.x-v0.x, v1.y-v0.y);
  
   logic [31:0] n_v2v1_x, r_v2v1_x;
   logic [31:0] n_v2v1_y, r_v2v1_y;
   
   logic [31:0] n_v0v2_x, r_v0v2_x;
   logic [31:0] n_v0v2_y, r_v0v2_y;

   logic [31:0] n_v1v0_x, r_v1v0_x;
   logic [31:0] n_v1v0_y, r_v1v0_y;
   
   logic [31:0] n_x, r_x;
   logic [31:0] n_y, r_y;
   logic [31:0] r_x_dim, n_x_dim;
   logic [31:0] r_addr, n_addr;
   logic [31:0] r_start_addr, n_start_addr;
   logic 	r_done, n_done;
   
   logic [31:0] r_w0, n_w0;
   logic [31:0] r_w1, n_w1;
   logic [31:0] r_w2, n_w2;

   logic [31:0] r_w0_y, n_w0_y;
   logic [31:0] r_w1_y, n_w1_y;
   logic [31:0] r_w2_y, n_w2_y;

   logic [DEPTH_W-1:0] r_z, n_z;
   logic [DEPTH_W-1:0] r_z_y, n_z_y;

   logic [SW_W-1:0] r_sw, n_sw;
   logic [SW_W-1:0] r_sw_y, n_sw_y;
   logic [SW_W-1:0] r_tw, n_tw;
   logic [SW_W-1:0] r_tw_y, n_tw_y;
   logic [IW_W-1:0] r_iw, n_iw;
   logic [IW_W-1:0] r_iw_y, n_iw_y;

   logic [COL_W-1:0] r_cr, n_cr, r_cr_y, n_cr_y;
   logic [COL_W-1:0] r_cg, n_cg, r_cg_y, n_cg_y;
   logic [COL_W-1:0] r_cb, n_cb, r_cb_y, n_cb_y;

   logic [31:0] t_mul_a, t_mul_b;
   logic [31:0] t_sub_a0, t_sub_b0;
   logic [31:0] t_sub_a1, t_sub_b1;
   logic [31:0] t_sub_a2, t_sub_b2;   

   wire [31:0]	w_sub0 = t_sub_a0 - t_sub_b0;
   wire [31:0]	w_sub1 = t_sub_a1 - t_sub_b1;
   wire [31:0]	w_sub2 = t_sub_a2 - t_sub_b2;   
   
   /* compute bounding box */
   wire [31:0] 	w_x_min0 = r_v0_x < r_v1_x ? r_v0_x : r_v1_x;
   wire [31:0] 	w_y_min0 = r_v0_y < r_v1_y ? r_v0_y : r_v1_y;
   wire [31:0] 	w_x_max0 = r_v0_x < r_v1_x ? r_v1_x : r_v0_x;
   wire [31:0] 	w_y_max0 = r_v0_y < r_v1_y ? r_v1_y : r_v0_y;
   wire [31:0] 	w_x_min = r_v2_x < w_x_min0 ? r_v2_x : w_x_min0;   
   wire [31:0] 	w_y_min = r_v2_y < w_y_min0 ? r_v2_y : w_y_min0;
   wire [31:0] 	w_x_max = r_v2_x < w_x_max0 ? w_x_max0 : r_v2_x;
   wire [31:0] 	w_y_max = r_v2_y < w_y_max0 ? w_y_max0 : r_v2_y;      

   
   logic [31:0] r_mul, rr_mul;
   always_ff@(posedge clk)
     begin
	r_mul <= t_mul_a * t_mul_b;
	rr_mul <= r_mul;
     end
   wire [31:0] w_mul = rr_mul;

   assign done = r_done;

   // no output queue any more: depth-passing fragments are shaded and written
   // straight into the on-chip framebuffer (below).  The pipe never stalls
   // (BRAM accepts one write/cycle), so the clock-enable is always on.
   wire w_pipe_en = 1'b1;

   // ---- perspective divide: 1/w reciprocal + u/w, t/w multiplies ----
   // The steppers produce (r_sw, r_tw, r_iw) per pixel.  Feed 1/w into the
   // pipelined reciprocal and carry (addr, depth, u/w, t/w) down a matching
   // 4-deep side-band so they align with the reciprocal's output, then recover
   //   u = (u/w) / (1/w),   v = (t/w) / (1/w)
   // and push (addr, depth, u, v) to the output queue.
   wire w_covered = (r_w0[31] | r_w1[31] | r_w2[31]) == 1'b0;
   wire w_feed    = (r_state == RENDER) & w_covered;

   wire        w_recip_valid, w_recip_busy;
   wire [17:0] w_recip_y;
   wire [4:0]  w_recip_e;
   recip u_recip(.clk(clk), .rst(rst), .en(w_pipe_en),
		 .valid_in(w_feed), .d(32'(r_iw)),
		 .valid_out(w_recip_valid), .busy(w_recip_busy),
		 .y(w_recip_y), .e(w_recip_e));

   // current-pixel Gouraud color: clamp each stepper's integer part to [0,255]
   function automatic logic [7:0] clamp8(input logic signed [COL_W-1:0] x);
      if(x < 0)        clamp8 = 8'd0;
      else if(x > 255) clamp8 = 8'd255;
      else             clamp8 = x[7:0];
   endfunction
   wire [23:0] w_col_now = {clamp8($signed(r_cr) >>> COL_FRAC),
			    clamp8($signed(r_cg) >>> COL_FRAC),
			    clamp8($signed(r_cb) >>> COL_FRAC)};

   // side-band: carry address, depth, u/w, t/w and the Gouraud color through
   // the same 4-cycle latency as the reciprocal (gated by the same enable)
   logic [31:0]     r_sb_addr  [0:3];
   logic [31:0]     r_sb_depth [0:3];
   logic [SW_W-1:0] r_sb_sw    [0:3];
   logic [SW_W-1:0] r_sb_tw    [0:3];
   logic [23:0]     r_sb_col   [0:3];
   always_ff@(posedge clk)
     if(w_pipe_en)
       begin
	  r_sb_addr[0]  <= r_addr;
	  r_sb_depth[0] <= 32'(r_z[DEPTH_W-1:DEPTH_FRAC_BITS]);
	  r_sb_sw[0]    <= r_sw;
	  r_sb_tw[0]    <= r_tw;
	  r_sb_col[0]   <= w_col_now;
	  for(int i = 1; i < 4; i++)
	    begin
	       r_sb_addr[i]  <= r_sb_addr[i-1];
	       r_sb_depth[i] <= r_sb_depth[i-1];
	       r_sb_sw[i]    <= r_sb_sw[i-1];
	       r_sb_tw[i]    <= r_sb_tw[i-1];
	       r_sb_col[i]   <= r_sb_col[i-1];
	    end
       end

   // recover u,v in Q16: 1/d ~= y*2^-(16+e), u = sw/d, so u*2^16 = (sw*y) >> e.
   // Two pipeline stages -- MUL (the two 48x19 products) then SHIFT (the
   // variable >>> e) -- carry addr/depth/valid alongside so they reach the
   // depth test together.  Gated by the same enable as the rest of the pipe.
   wire signed [18:0] w_sy = $signed({1'b0, w_recip_y});

   // MUL stage
   logic              r_m_valid;
   logic [31:0]       r_m_addr;
   logic [Z_W-1:0]    r_m_depth;
   logic [4:0]        r_m_e;
   logic [23:0]       r_m_col;
   logic [17:0]       r_m_wq4;     // w in Q4 (w*16 = y>>e), for per-pixel LOD
   logic signed [66:0] r_m_uprod, r_m_vprod;
   always_ff@(posedge clk)
     begin
	if(rst) r_m_valid <= 1'b0;
	else if(w_pipe_en) r_m_valid <= w_recip_valid & (r_sb_addr[3] < FB_N);
	if(w_pipe_en)
	  begin
	     r_m_addr  <= r_sb_addr[3];
	     r_m_depth <= r_sb_depth[3][Z_W-1:0];
	     r_m_e     <= w_recip_e;
	     r_m_col   <= r_sb_col[3];
	     r_m_wq4   <= w_recip_y >> w_recip_e;
	     r_m_uprod <= $signed(r_sb_sw[3]) * w_sy;
	     r_m_vprod <= $signed(r_sb_tw[3]) * w_sy;
	  end
     end

   // SHIFT stage
   wire signed [66:0] w_ush = r_m_uprod >>> r_m_e;
   wire signed [66:0] w_vsh = r_m_vprod >>> r_m_e;
   logic           r_s_valid;
   logic [31:0]    r_s_addr;
   logic [Z_W-1:0] r_s_depth;
   logic [23:0]    r_s_col;
   logic [17:0]    r_s_wq4;
   logic [31:0]    r_s_u, r_s_v;
   always_ff@(posedge clk)
     begin
	if(rst) r_s_valid <= 1'b0;
	else if(w_pipe_en) r_s_valid <= r_m_valid;
	if(w_pipe_en)
	  begin
	     r_s_addr  <= r_m_addr;
	     r_s_depth <= r_m_depth;
	     r_s_col   <= r_m_col;
	     r_s_wq4   <= r_m_wq4;
	     r_s_u     <= w_ush[31:0];
	     r_s_v     <= w_vsh[31:0];
	  end
     end

   // ---- hardware depth buffer (BRAM) + depth test ----
   // One pipeline stage (Z1) absorbs the BRAM read latency: it registers the
   // reciprocal-stage fragment and the depth read at its address, then the
   // next cycle compares (nearest wins) and, on pass, writes the new depth and
   // pushes (addr,u,v) to the output queue.  Only depth-passing fragments are
   // queued, so the consumer needs no depth value.
   logic [Z_W-1:0] r_zb [0:FB_N-1];

   // clear: walk the buffer writing the far value (0xFFFF) once per frame
   logic [FB_AW:0] r_clear_cnt;
   logic 	   r_clearing;
   assign clearing = r_clearing;
   always_ff@(posedge clk)
     begin
	if(rst)
	  begin r_clearing <= 1'b0; r_clear_cnt <= 'd0; end
	else if(clear)
	  begin r_clearing <= 1'b1; r_clear_cnt <= 'd0; end
	else if(r_clearing)
	  begin
	     r_clear_cnt <= r_clear_cnt + 'd1;
	     if(r_clear_cnt == FB_N-1) r_clearing <= 1'b0;
	  end
     end

   // ---- per-pixel level of detail (analytic uv Jacobian) ----
   // du/dx = w*(Px - u*Wx) etc., with Px=dswdx... (Q12), u,v in Q16, w carried
   // as w*16 (r_s_wq4).  A_* = (deriv)*2^12; LOD = log2(TEX_W * w * max|A|).
   wire signed [63:0] w_uWx = ($signed(r_s_u) * $signed(diwdx)) >>> 16;
   wire signed [63:0] w_uWy = ($signed(r_s_u) * $signed(diwdy)) >>> 16;
   wire signed [63:0] w_vWx = ($signed(r_s_v) * $signed(diwdx)) >>> 16;
   wire signed [63:0] w_vWy = ($signed(r_s_v) * $signed(diwdy)) >>> 16;
   wire signed [63:0] w_Aux = $signed({{16{dswdx[SW_W-1]}}, dswdx}) - w_uWx;
   wire signed [63:0] w_Auy = $signed({{16{dswdy[SW_W-1]}}, dswdy}) - w_uWy;
   wire signed [63:0] w_Avx = $signed({{16{dtwdx[SW_W-1]}}, dtwdx}) - w_vWx;
   wire signed [63:0] w_Avy = $signed({{16{dtwdy[SW_W-1]}}, dtwdy}) - w_vWy;
   wire [63:0] w_mux = w_Aux[63] ? (~w_Aux + 64'd1) : w_Aux;
   wire [63:0] w_muy = w_Auy[63] ? (~w_Auy + 64'd1) : w_Auy;
   wire [63:0] w_mvx = w_Avx[63] ? (~w_Avx + 64'd1) : w_Avx;
   wire [63:0] w_mvy = w_Avy[63] ? (~w_Avy + 64'd1) : w_Avy;
   wire [63:0] w_mmx = (w_mux > w_muy) ? w_mux : w_muy;
   wire [63:0] w_mmy = (w_mvx > w_mvy) ? w_mvx : w_mvy;
   wire [63:0] w_M   = (w_mmx > w_mmy) ? w_mmx : w_mmy;       // max|A|, Q12
   wire [81:0] w_fp  = w_M * r_s_wq4;                         // footprint_texels * 2^9
   logic [6:0] w_msb;
   always_comb begin
      w_msb = 7'd0;
      for(int i = 0; i < 82; i++) if(w_fp[i]) w_msb = i[6:0];
   end
   wire signed [8:0] w_lods = $signed({2'b0, w_msb}) - 9'sd9;  // log2 = msb + TEX_LW - 16
   wire [2:0] w_lod = (w_lods < 0) ? 3'd0 : (w_lods > 9'sd7) ? 3'd7 : w_lods[2:0];

   // ---- texel coordinate at mip level L = w_lod ----
   // texel coord = u*(TEX_W>>L); half-texel offset; base texel + Q(TEX_FB) frac.
   // L scales the shift (9+L) and the REPEAT wrap mask ((TEX_W>>L)-1).
   wire [3:0]         w_sh   = TEX_FB[3:0] + {1'b0, w_lod};      // 9 + L
   wire signed [31:0] w_half = $signed(32'd1) << (w_sh - 4'd1);  // 0.5 texel
   wire signed [31:0] w_su = $signed(r_s_u) - w_half;
   wire signed [31:0] w_sv = $signed(r_s_v) - w_half;
   wire signed [31:0] w_i0 = w_su >>> w_sh;
   wire signed [31:0] w_j0 = w_sv >>> w_sh;
   wire [TEX_FB-1:0]  w_fx = w_su[{2'b0, w_lod} +: TEX_FB];     // frac bits above L
   wire [TEX_FB-1:0]  w_fy = w_sv[{2'b0, w_lod} +: TEX_FB];
   wire               w_px = w_i0[0];
   wire               w_py = w_j0[0];
   // REPEAT wrap within the level (low (TEX_LW-L) bits; L=0 wraps via 7-bit math)
   wire [TEX_LW-1:0]  w_mask = (7'd1 << (TEX_LW[2:0] - w_lod)) - 7'd1;
   wire [TEX_LW-1:0] w_x0 = w_i0[TEX_LW-1:0] & w_mask;
   wire [TEX_LW-1:0] w_x1 = (w_i0[TEX_LW-1:0] + 1'b1) & w_mask;
   wire [TEX_LW-1:0] w_y0 = w_j0[TEX_LW-1:0] & w_mask;
   wire [TEX_LW-1:0] w_y1 = (w_j0[TEX_LW-1:0] + 1'b1) & w_mask;
   // column/row by parity; per-bank address = {level, row>>1, col>>1}
   wire [TEX_LW-1:0] w_colp0 = w_px ? w_x1 : w_x0;   // x-parity 0 column
   wire [TEX_LW-1:0] w_colp1 = w_px ? w_x0 : w_x1;   // x-parity 1 column
   wire [TEX_LW-1:0] w_rowp0 = w_py ? w_y1 : w_y0;
   wire [TEX_LW-1:0] w_rowp1 = w_py ? w_y0 : w_y1;
   wire [TEXB_AW-1:0] w_ab0 = {w_lod, w_rowp0[TEX_LW-1:1], w_colp0[TEX_LW-1:1]};
   wire [TEXB_AW-1:0] w_ab1 = {w_lod, w_rowp0[TEX_LW-1:1], w_colp1[TEX_LW-1:1]};
   wire [TEXB_AW-1:0] w_ab2 = {w_lod, w_rowp1[TEX_LW-1:1], w_colp0[TEX_LW-1:1]};
   wire [TEXB_AW-1:0] w_ab3 = {w_lod, w_rowp1[TEX_LW-1:1], w_colp1[TEX_LW-1:1]};

   // ---- texture: 4 banks split by (x&1,y&1) -> the 2x2 quad is 1 texel per
   // bank, so all four read in parallel.  Host loads linearly; route to bank
   // {tv[0],tu[0]} at addr (tv>>1,tu>>1).
   logic [23:0] r_tex0 [0:TEXB_N-1];  // x even, y even
   logic [23:0] r_tex1 [0:TEXB_N-1];  // x odd,  y even
   logic [23:0] r_tex2 [0:TEXB_N-1];  // x even, y odd
   logic [23:0] r_tex3 [0:TEXB_N-1];  // x odd,  y odd
   logic [23:0] r_z1_b0, r_z1_b1, r_z1_b2, r_z1_b3;
   always_ff@(posedge clk)
     begin
	if(tex_we & (tex_wbank == 2'd0)) r_tex0[tex_waddr[TEXB_AW-1:0]] <= tex_wdata;
	if(tex_we & (tex_wbank == 2'd1)) r_tex1[tex_waddr[TEXB_AW-1:0]] <= tex_wdata;
	if(tex_we & (tex_wbank == 2'd2)) r_tex2[tex_waddr[TEXB_AW-1:0]] <= tex_wdata;
	if(tex_we & (tex_wbank == 2'd3)) r_tex3[tex_waddr[TEXB_AW-1:0]] <= tex_wdata;
	if(w_pipe_en)
	  begin
	     r_z1_b0 <= r_tex0[w_ab0];
	     r_z1_b1 <= r_tex1[w_ab1];
	     r_z1_b2 <= r_tex2[w_ab2];
	     r_z1_b3 <= r_tex3[w_ab3];
	  end
     end

   // Z1 stage: fragment + depth read + the quad fractions/parities, aligned
   logic        r_z1_valid;
   logic [31:0] r_z1_addr;
   logic [Z_W-1:0] r_z1_depth;
   logic [23:0] r_z1_col;
   logic [TEX_FB-1:0] r_z1_fx, r_z1_fy;
   logic        r_z1_px, r_z1_py;
   logic [Z_W-1:0] r_z1_zold;
   always_ff@(posedge clk)
     begin
	if(rst) r_z1_valid <= 1'b0;
	else if(w_pipe_en) r_z1_valid <= r_s_valid;
	if(w_pipe_en)
	  begin
	     r_z1_addr  <= r_s_addr;
	     r_z1_depth <= r_s_depth;
	     r_z1_col   <= r_s_col;
	     r_z1_fx    <= w_fx;
	     r_z1_fy    <= w_fy;
	     r_z1_px    <= w_px;
	     r_z1_py    <= w_py;
	  end
     end

   wire w_zpass     = r_z1_valid & (r_z1_depth < r_z1_zold);
   wire w_frag_wr   = w_pipe_en & w_zpass;     // commit this fragment

   // depth BRAM: clear takes priority, else conditional depth write; one read
   // port issues the depth at the SHIFT-stage address (Z1's input) each cycle.
   always_ff@(posedge clk)
     begin
	if(r_clearing)
	  r_zb[r_clear_cnt[FB_AW-1:0]] <= {Z_W{1'b1}};
	else if(w_frag_wr)
	  r_zb[r_z1_addr[FB_AW-1:0]] <= r_z1_depth;
	if(w_pipe_en)
	  r_z1_zold <= r_zb[r_s_addr[FB_AW-1:0]];
     end

   // ---- bilinear blend + GL_MODULATE ----
   // un-swizzle the 4 banks into corners by (px,py), then lerp in x then y.
   function automatic logic [7:0] lerp8(input logic [7:0] a, input logic [7:0] b,
					input logic [TEX_FB-1:0] f);
      logic signed [9:0]  d;
      logic signed [9:0]  fs;
      logic signed [19:0] prod;
      logic signed [19:0] res;
      begin
	 d    = $signed({2'b0, b}) - $signed({2'b0, a});
	 fs   = $signed({1'b0, f});
	 prod = d * fs;
	 res  = $signed({12'b0, a}) + (prod >>> TEX_FB);
	 lerp8 = res[7:0];
      end
   endfunction
   // out_c = floor(texel_c * color_c / 255), exact via ((x+1)*257)>>16.
   function automatic logic [7:0] mul255(input logic [7:0] a, input logic [7:0] b);
      logic [15:0] p;
      logic [16:0] s;
      logic [25:0] t;
      begin
	 p = a * b;
	 s = {1'b0, p} + 17'd1;
	 t = s * 9'd257;
	 mul255 = t[23:16];
      end
   endfunction

   logic [23:0] w_bank [0:3];
   assign w_bank[0] = r_z1_b0;
   assign w_bank[1] = r_z1_b1;
   assign w_bank[2] = r_z1_b2;
   assign w_bank[3] = r_z1_b3;
   wire [23:0] w_t00 = w_bank[{r_z1_py,  r_z1_px}];
   wire [23:0] w_t10 = w_bank[{r_z1_py, ~r_z1_px}];
   wire [23:0] w_t01 = w_bank[{~r_z1_py, r_z1_px}];
   wire [23:0] w_t11 = w_bank[{~r_z1_py,~r_z1_px}];
   wire [7:0] w_tr = lerp8(lerp8(w_t00[23:16], w_t10[23:16], r_z1_fx),
			   lerp8(w_t01[23:16], w_t11[23:16], r_z1_fx), r_z1_fy);
   wire [7:0] w_tg = lerp8(lerp8(w_t00[15:8],  w_t10[15:8],  r_z1_fx),
			   lerp8(w_t01[15:8],  w_t11[15:8],  r_z1_fx), r_z1_fy);
   wire [7:0] w_tb = lerp8(lerp8(w_t00[7:0],   w_t10[7:0],   r_z1_fx),
			   lerp8(w_t01[7:0],   w_t11[7:0],   r_z1_fx), r_z1_fy);

   wire [23:0] w_color = {mul255(w_tr, r_z1_col[23:16]),
			  mul255(w_tg, r_z1_col[15:8]),
			  mul255(w_tb, r_z1_col[7:0])};

   // ---- color framebuffer (BRAM): clear, conditional write, host read-out ----
   logic [23:0] r_fb [0:FB_N-1];
   always_ff@(posedge clk)
     begin
	if(r_clearing)
	  r_fb[r_clear_cnt[FB_AW-1:0]] <= 24'h0;
	else if(w_frag_wr)
	  r_fb[r_z1_addr[FB_AW-1:0]] <= w_color;
	fb_rdata <= r_fb[fb_raddr[FB_AW-1:0]];
     end

   logic [31:0] t_w0, t_w1, t_w2;
   always_comb
     begin
	t_w0 = 'd0;
	t_w1 = 'd0;
	t_w2 = 'd0;
	
	// if(r_state == COMPUTE_START_CROSS_V0V1_X)
	//   begin
	//      t_w0  = crossp(r_v0_x, r_v0_y, 
	// 		    r_v1_x, r_v1_y,
	// 		    r_x_min, r_y_min);
	     
	//      t_w1  = crossp(r_v2_x, r_v2_y, 
	// 		    r_v0_x, r_v0_y,
	// 		    r_x_min, r_y_min);
	     
	//      t_w2  = crossp(r_v1_x, r_v1_y, 
	// 		    r_v2_x, r_v2_y,
	// 		    r_x_min, r_y_min);
	//   end

	
     end // always_comb

   
   always_comb
     begin
	n_state = r_state;
	n_x_min = r_x_min;
	n_y_min = r_y_min;
	n_x_max = r_x_max;
	n_y_max = r_y_max;
	n_x_dim = r_x_dim;
	n_v2v1_x = r_v2v1_x;
	n_v2v1_y = r_v2v1_y;
	n_v0v2_x = r_v0v2_x;
	n_v0v2_y = r_v0v2_y;
	n_v1v0_x = r_v1v0_x;
	n_v1v0_y = r_v1v0_y;
	n_x = r_x;
	n_y = r_y;
	n_start_addr = r_start_addr;
	n_addr = r_addr;
	n_done = 1'b0;
	t_mul_a = 32'd0;
	t_mul_b = 32'd0;
	t_sub_a0 = 32'd0;
	t_sub_a1 = 32'd0;
	t_sub_a2 = 32'd0;
	t_sub_b0 = 32'd0;
	t_sub_b1 = 32'd0;
	t_sub_b2 = 32'd0;		
	
	n_w0 = r_w0;
	n_w1 = r_w1;
	n_w2 = r_w2;

	n_w0_y = r_w0_y;
	n_w1_y = r_w1_y;
	n_w2_y = r_w2_y;

	n_z = r_z;
	n_z_y = r_z_y;

	n_sw = r_sw;
	n_sw_y = r_sw_y;
	n_tw = r_tw;
	n_tw_y = r_tw_y;
	n_iw = r_iw;
	n_iw_y = r_iw_y;

	n_cr = r_cr; n_cr_y = r_cr_y;
	n_cg = r_cg; n_cg_y = r_cg_y;
	n_cb = r_cb; n_cb_y = r_cb_y;

	t_save_tri = 1'b0;

	case(r_state)
	  IDLE:
	    begin
	       if(go)
		 begin
		    n_state = COMPUTE_BB;
		    t_save_tri = 1'b1;
		    n_x_dim = x_dim;
		    //weird ordering is correct
		    n_w0 = w2;
		    n_w0_y = w2;
		    n_w1 = w1;
		    n_w1_y = w1;
		    n_w2 = w0;
		    n_w2_y = w0;
		    n_z = z_start;
		    n_z_y = z_start;
		    n_sw = sw_start;
		    n_sw_y = sw_start;
		    n_tw = tw_start;
		    n_tw_y = tw_start;
		    n_iw = iw_start;
		    n_iw_y = iw_start;
		    n_cr = cr_start; n_cr_y = cr_start;
		    n_cg = cg_start; n_cg_y = cg_start;
		    n_cb = cb_start; n_cb_y = cb_start;
		 end
	    end
	  COMPUTE_BB:
	    begin
	       n_x_min = w_x_min;
	       n_y_min = w_y_min;
	       n_x_max = w_x_max;
	       n_y_max = w_y_max;
	       n_y = w_y_min;
	       n_x = w_x_min;
	       n_state = COMPUTE_XVEC;
	    end
	  COMPUTE_XVEC:
	    begin
	       t_sub_a0 = r_v2_x;
	       t_sub_b0 = r_v1_x;
	       n_v2v1_x = w_sub0;
	       t_sub_a1 = r_v0_x;
	       t_sub_b1 = r_v2_x;
	       n_v0v2_x = w_sub1;
	       t_sub_a2 = r_v1_x;
	       t_sub_b2 = r_v0_x;	       
	       n_v1v0_x = w_sub2;
	       n_state = COMPUTE_YVEC;
	    end
	  COMPUTE_YVEC:
	    begin
	       t_sub_a0 = r_v2_y;
	       t_sub_b0 = r_v1_y;
	       n_v2v1_y = w_sub0;
	       t_sub_a1 = r_v0_y;
	       t_sub_b1 = r_v2_y;
	       n_v0v2_y = w_sub1;
	       t_sub_a2 = r_v1_y;
	       t_sub_b2 = r_v0_y;	       
	       n_v1v0_y = w_sub2;
	       n_state = COMPUTE_START_ADDR;
	    end
	  COMPUTE_START_ADDR:
	    begin
	       t_mul_a = r_x_dim;
	       t_mul_b = r_y;
	       n_state = COMPUTE_START_CROSS_V1V2_X;
	    end
	  COMPUTE_START_CROSS_V1V2_X:
	    begin
	       n_state = COMPUTE_START_CROSS_V2V0_X;
	    end
	  COMPUTE_START_CROSS_V2V0_X:
	    begin
	       n_state = COMPUTE_START_CROSS_V0V1_X;
	       n_start_addr = w_mul + r_x_min;
	    end
	  COMPUTE_START_CROSS_V0V1_X:
	    begin
	       n_addr = r_start_addr;
	       n_state = RENDER;
	    end
	  RENDER:
	    begin
	       if(r_x == r_x_max)
		      begin
			 n_x = w_x_min;
			 n_y = r_y + 'd1;
			 n_start_addr = r_start_addr + r_x_dim;
			 n_addr = r_start_addr + r_x_dim;

			 t_sub_a0 = r_w0_y;
			 t_sub_b0 = (~r_v2v1_x) + 32'd1;
			 n_w0 = w_sub0;
			 n_w0_y = w_sub0;			 
			 
			 t_sub_a1 = r_w1_y;
			 t_sub_b1 = (~r_v0v2_x) + 32'd1;
			 n_w1 = w_sub1;
			 n_w1_y = w_sub1;			 
			 
			 t_sub_a2 = r_w2_y;
			 t_sub_b2 = (~r_v1v0_x) + 32'd1;	
			 n_w2 = w_sub2;
			 n_w2_y = w_sub2;
			 n_z = r_z_y + dzdy;
			 n_z_y = r_z_y + dzdy;
			 n_sw = r_sw_y + dswdy;
			 n_sw_y = r_sw_y + dswdy;
			 n_tw = r_tw_y + dtwdy;
			 n_tw_y = r_tw_y + dtwdy;
			 n_iw = r_iw_y + diwdy;
			 n_iw_y = r_iw_y + diwdy;
			 n_cr = r_cr_y + dcrdy; n_cr_y = r_cr_y + dcrdy;
			 n_cg = r_cg_y + dcgdy; n_cg_y = r_cg_y + dcgdy;
			 n_cb = r_cb_y + dcbdy; n_cb_y = r_cb_y + dcbdy;
			 //$display("n_start_addr = %d, n_y = %d", n_start_addr, n_y);
		      end
		    else
		      begin
			 n_x = r_x + 'd1;
			 n_addr = r_addr + 'd1;
			 
			 t_sub_a0 = r_w0;
			 t_sub_b0 = r_v2v1_y;
			 n_w0 = w_sub0;
			 
			 t_sub_a1 = r_w1;
			 t_sub_b1 = r_v0v2_y;
			 n_w1 = w_sub1;
			 
			 t_sub_a2 = r_w2;
			 t_sub_b2 = r_v1v0_y;	       
			 n_w2 = w_sub2;
			 n_z = r_z + dzdx;
			 n_sw = r_sw + dswdx;
			 n_tw = r_tw + dtwdx;
			 n_iw = r_iw + diwdx;
			 n_cr = r_cr + dcrdx;
			 n_cg = r_cg + dcgdx;
			 n_cb = r_cb + dcbdx;

		      end // else: !if(r_x == r_x_max)
		    
		    if((r_y == w_y_max) & (r_x == r_x_max))
		      begin
			 n_state = DRAIN;
		      end
	    end
	  DRAIN:
	    begin
	       // wait for the reciprocal, multiply and depth-test stages to flush
	       if(!w_recip_busy & !r_m_valid & !r_s_valid & !r_z1_valid)
		 begin
		    n_done = 1'b1;
		    n_state = IDLE;
		 end
	    end
	  default:
	    begin
	    end
	endcase	
     end // always_comb

   // always_ff@(negedge clk)
   //   begin
   // 	if((r_state == RENDER) & w_queue_not_full)
   // 	  begin
   // 	     check_frag(r_x, r_y, r_w0, r_w1, r_w2);
   // 	  end
   //   end

   always_ff@(posedge clk)
     begin
	if(t_save_tri)
	  begin
	     r_v0_x <= v0_x;
	     r_v0_y <= v0_y;
	     r_v1_x <= v1_x;
	     r_v1_y <= v1_y;
	     r_v2_x <= v2_x;
	     r_v2_y <= v2_y;
	  end
     end
   
   always_ff@(posedge clk)
     begin
	r_state <= rst ? IDLE: n_state;
	r_x_min <= rst ? 'd0 : n_x_min;
	r_x_max <= rst ? 'd0 : n_x_max;
	r_y_min <= rst ? 'd0 : n_y_min;
	r_y_max <= rst ? 'd0 : n_y_max;
	r_y <= rst ? 'd0 : n_y;
	r_x <= rst ? 'd0 : n_x;
	r_x_dim <= rst ? 'd0 : n_x_dim;
	r_addr <= rst ? 'd0  : n_addr;
	r_start_addr <= rst ? 'd0  : n_start_addr;
	r_done <= rst ? 1'b0 : n_done;
	r_v2v1_x <= rst ? 'd0 : n_v2v1_x;
	r_v2v1_y <= rst ? 'd0 : n_v2v1_y;
	r_v0v2_x <= rst ? 'd0 : n_v0v2_x;
	r_v0v2_y <= rst ? 'd0 : n_v0v2_y;
	r_v1v0_x <= rst ? 'd0 : n_v1v0_x;
	r_v1v0_y <= rst ? 'd0 : n_v1v0_y;
	r_w0 <= rst ? 'd0 : n_w0;
	r_w1 <= rst ? 'd0 : n_w1;
	r_w2 <= rst ? 'd0 : n_w2;
	r_w0_y <= rst ? 'd0 : n_w0_y;
	r_w1_y <= rst ? 'd0 : n_w1_y;
	r_w2_y <= rst ? 'd0 : n_w2_y;
	r_z <= rst ? 'd0 : n_z;
	r_z_y <= rst ? 'd0 : n_z_y;
	r_sw <= rst ? 'd0 : n_sw;
	r_sw_y <= rst ? 'd0 : n_sw_y;
	r_tw <= rst ? 'd0 : n_tw;
	r_tw_y <= rst ? 'd0 : n_tw_y;
	r_iw <= rst ? 'd0 : n_iw;
	r_iw_y <= rst ? 'd0 : n_iw_y;
	r_cr <= rst ? 'd0 : n_cr;
	r_cr_y <= rst ? 'd0 : n_cr_y;
	r_cg <= rst ? 'd0 : n_cg;
	r_cg_y <= rst ? 'd0 : n_cg_y;
	r_cb <= rst ? 'd0 : n_cb;
	r_cb_y <= rst ? 'd0 : n_cb_y;
     end // always_ff@ (posedge clk)
   
endmodule // rasterize

