// AXI4-Lite wrapper around rasterize for the Ultra96 (ZU3EG) standalone build.
// Plain Verilog top so it can be a block-design module reference; rasterize
// itself is SystemVerilog.  Build with SCREEN_WIDTH=320 SCREEN_HEIGHT=200
// defined (Vivado verilog_define / Verilator +define+).
//
// Register map (byte offsets, 32-bit full-word writes only -- wstrb ignored):
//   0x00 ID       RO  0x52415354 ("RAST")
//   0x04 CTRL     WO  bit0 go, bit1 clear, bit2 swap (one-cycle pulses)
//   0x08 STATUS   RO  bit0 busy (go .. done), bit1 clearing
//   0x0c X_DIM, 0x10 Y_DIM
//   0x14 TEX_ADDR     [31:30] bank, [29:0] bank address
//   0x18 TEX_DATA WO  texel [23:0] written at TEX_ADDR, then TEX_ADDR++
//   0x1c FB_ADDR      front-buffer scan-out address
//   0x20 FB_DATA  RO  pixel [23:0] at FB_ADDR, then FB_ADDR++
//   0x24 MODE         [1:0] blend_mode, [15:8] alpha, [16] z_enable
//   0x40.. triangle setup, see the I_* indices below (64-bit planes = lo, hi)
//
// The rasterizer reads the gradient inputs for the whole render, so the host
// must not write triangle registers while STATUS.busy is set.

module rast_axi(s_axi_aclk, s_axi_aresetn,
		s_axi_awaddr, s_axi_awprot, s_axi_awvalid, s_axi_awready,
		s_axi_wdata, s_axi_wstrb, s_axi_wvalid, s_axi_wready,
		s_axi_bresp, s_axi_bvalid, s_axi_bready,
		s_axi_araddr, s_axi_arprot, s_axi_arvalid, s_axi_arready,
		s_axi_rdata, s_axi_rresp, s_axi_rvalid, s_axi_rready);
   input wire s_axi_aclk;
   input wire s_axi_aresetn;
   input wire [9:0] s_axi_awaddr;
   input wire [2:0] s_axi_awprot;
   input wire 	    s_axi_awvalid;
   output wire 	    s_axi_awready;
   input wire [31:0] s_axi_wdata;
   input wire [3:0]  s_axi_wstrb;
   input wire 	     s_axi_wvalid;
   output wire 	     s_axi_wready;
   output wire [1:0] s_axi_bresp;
   output wire 	     s_axi_bvalid;
   input wire 	     s_axi_bready;
   input wire [9:0]  s_axi_araddr;
   input wire [2:0]  s_axi_arprot;
   input wire 	     s_axi_arvalid;
   output wire 	     s_axi_arready;
   output wire [31:0] s_axi_rdata;
   output wire [1:0]  s_axi_rresp;
   output wire 	      s_axi_rvalid;
   input wire 	      s_axi_rready;

   localparam I_ID = 0, I_CTRL = 1, I_STATUS = 2, I_X_DIM = 3, I_Y_DIM = 4;
   localparam I_TEX_ADDR = 5, I_TEX_DATA = 6, I_FB_ADDR = 7, I_FB_DATA = 8, I_MODE = 9;
   localparam I_V0X = 16, I_V0Y = 17, I_V1X = 18, I_V1Y = 19, I_V2X = 20, I_V2Y = 21;
   localparam I_W0 = 22, I_W1 = 23, I_W2 = 24;
   localparam I_DZDX = 25, I_DZDY = 27, I_Z_START = 29;
   localparam I_SW_START = 31, I_DSWDX = 33, I_DSWDY = 35;
   localparam I_TW_START = 37, I_DTWDX = 39, I_DTWDY = 41;
   localparam I_IW_START = 43, I_DIWDX = 44, I_DIWDY = 45;
   localparam I_CR_START = 46, I_DCRDX = 47, I_DCRDY = 48;
   localparam I_CG_START = 49, I_DCGDX = 50, I_DCGDY = 51;
   localparam I_CB_START = 52, I_DCBDX = 53, I_DCBDY = 54;
   localparam N_REGS = 64;

   wire       clk = s_axi_aclk;
   wire       rst = ~s_axi_aresetn;

   reg [31:0] r_regs [0:N_REGS-1];
   reg [31:0] r_tex_addr, r_fb_addr;
   reg [23:0] r_tex_wdata;
   reg 	      r_tex_we, r_go, r_clear, r_swap, r_busy;
   reg 	      r_bvalid, r_rvalid;
   reg [31:0] r_rdata;

   wire       w_done, w_clearing;
   wire [23:0] w_fb_rdata;

   // write: take AW and W together, one outstanding response
   wire        w_wr = s_axi_awvalid & s_axi_wvalid & ~r_bvalid;
   wire [5:0]  w_widx = s_axi_awaddr[7:2];
   // read: one outstanding
   wire        w_rd = s_axi_arvalid & ~r_rvalid;
   wire [5:0]  w_ridx = s_axi_araddr[7:2];

   assign s_axi_awready = w_wr;
   assign s_axi_wready = w_wr;
   assign s_axi_bresp = 2'b00;
   assign s_axi_bvalid = r_bvalid;
   assign s_axi_arready = w_rd;
   assign s_axi_rdata = r_rdata;
   assign s_axi_rresp = 2'b00;
   assign s_axi_rvalid = r_rvalid;

   always @(posedge clk)
     begin
	if(w_wr)
	  begin
	     r_regs[w_widx] <= s_axi_wdata;
	  end
     end // always

   always @(posedge clk)
     begin
	if(rst)
	  begin
	     r_bvalid <= 1'b0;
	     r_rvalid <= 1'b0;
	     r_rdata <= 32'd0;
	     r_go <= 1'b0;
	     r_clear <= 1'b0;
	     r_swap <= 1'b0;
	     r_busy <= 1'b0;
	     r_tex_we <= 1'b0;
	     r_tex_wdata <= 24'd0;
	     r_tex_addr <= 32'd0;
	     r_fb_addr <= 32'd0;
	  end
	else
	  begin
	     r_bvalid <= w_wr ? 1'b1 : (s_axi_bready ? 1'b0 : r_bvalid);
	     r_rvalid <= w_rd ? 1'b1 : (s_axi_rready ? 1'b0 : r_rvalid);
	     r_go <= w_wr & (w_widx == I_CTRL) & s_axi_wdata[0];
	     r_clear <= w_wr & (w_widx == I_CTRL) & s_axi_wdata[1];
	     r_swap <= w_wr & (w_widx == I_CTRL) & s_axi_wdata[2];
	     r_busy <= r_go ? 1'b1 : (w_done ? 1'b0 : r_busy);
	     r_tex_we <= w_wr & (w_widx == I_TEX_DATA);
	     if(w_wr & (w_widx == I_TEX_DATA))
	       begin
		  r_tex_wdata <= s_axi_wdata[23:0];
	       end
	     // the texel goes out with the old address; step after it
	     if(w_wr & (w_widx == I_TEX_ADDR))
	       begin
		  r_tex_addr <= s_axi_wdata;
	       end
	     else if(r_tex_we)
	       begin
		  r_tex_addr <= {r_tex_addr[31:30], r_tex_addr[29:0] + 30'd1};
	       end
	     // fb_rdata reflects r_fb_addr from the previous cycle; reads are
	     // at least two cycles apart, so it is always settled here.
	     if(w_wr & (w_widx == I_FB_ADDR))
	       begin
		  r_fb_addr <= s_axi_wdata;
	       end
	     else if(w_rd & (w_ridx == I_FB_DATA))
	       begin
		  r_fb_addr <= r_fb_addr + 32'd1;
	       end
	     if(w_rd)
	       begin
		  r_rdata <= (w_ridx == I_ID) ? 32'h52415354 :
			     (w_ridx == I_STATUS) ? {30'd0, w_clearing | r_clear, r_busy | r_go} :
			     (w_ridx == I_TEX_ADDR) ? r_tex_addr :
			     (w_ridx == I_FB_ADDR) ? r_fb_addr :
			     (w_ridx == I_FB_DATA) ? {8'd0, w_fb_rdata} :
			     r_regs[w_ridx];
	       end
	  end
     end // always

   rasterize rast(.clk(clk),
		  .rst(rst),
		  .go(r_go),
		  .v0_x(r_regs[I_V0X]),
		  .v0_y(r_regs[I_V0Y]),
		  .v1_x(r_regs[I_V1X]),
		  .v1_y(r_regs[I_V1Y]),
		  .v2_x(r_regs[I_V2X]),
		  .v2_y(r_regs[I_V2Y]),
		  .w0(r_regs[I_W0]),
		  .w1(r_regs[I_W1]),
		  .w2(r_regs[I_W2]),
		  .dzdx({r_regs[I_DZDX+1][4:0], r_regs[I_DZDX]}),
		  .dzdy({r_regs[I_DZDY+1][4:0], r_regs[I_DZDY]}),
		  .z_start({r_regs[I_Z_START+1][4:0], r_regs[I_Z_START]}),
		  .dswdx({r_regs[I_DSWDX+1][15:0], r_regs[I_DSWDX]}),
		  .dswdy({r_regs[I_DSWDY+1][15:0], r_regs[I_DSWDY]}),
		  .sw_start({r_regs[I_SW_START+1][15:0], r_regs[I_SW_START]}),
		  .dtwdx({r_regs[I_DTWDX+1][15:0], r_regs[I_DTWDX]}),
		  .dtwdy({r_regs[I_DTWDY+1][15:0], r_regs[I_DTWDY]}),
		  .tw_start({r_regs[I_TW_START+1][15:0], r_regs[I_TW_START]}),
		  .diwdx(r_regs[I_DIWDX][29:0]),
		  .diwdy(r_regs[I_DIWDY][29:0]),
		  .iw_start(r_regs[I_IW_START][29:0]),
		  .dcrdx(r_regs[I_DCRDX][23:0]),
		  .dcrdy(r_regs[I_DCRDY][23:0]),
		  .cr_start(r_regs[I_CR_START][23:0]),
		  .dcgdx(r_regs[I_DCGDX][23:0]),
		  .dcgdy(r_regs[I_DCGDY][23:0]),
		  .cg_start(r_regs[I_CG_START][23:0]),
		  .dcbdx(r_regs[I_DCBDX][23:0]),
		  .dcbdy(r_regs[I_DCBDY][23:0]),
		  .cb_start(r_regs[I_CB_START][23:0]),
		  .blend_mode(r_regs[I_MODE][1:0]),
		  .tri_alpha(r_regs[I_MODE][15:8]),
		  .z_enable(r_regs[I_MODE][16]),
		  .tex_we(r_tex_we),
		  .tex_wbank(r_tex_addr[31:30]),
		  .tex_waddr({2'b00, r_tex_addr[29:0]}),
		  .tex_wdata(r_tex_wdata),
		  .x_dim(r_regs[I_X_DIM]),
		  .y_dim(r_regs[I_Y_DIM]),
		  .clear(r_clear),
		  .swap(r_swap),
		  .fb_raddr(r_fb_addr),
		  .fb_rdata(w_fb_rdata),
		  .clearing(w_clearing),
		  .done(w_done));

endmodule // rast_axi
