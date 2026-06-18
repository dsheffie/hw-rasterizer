
module rasterize(clk, rst, go,
		 v0_x, v0_y,
		 v1_x, v1_y,
		 v2_x, v2_y,
		 w0,w1,w2,
		 dzdx, dzdy, z_start,
		 dswdx, dswdy, sw_start,
		 dtwdx, dtwdy, tw_start,
		 diwdx, diwdy, iw_start,
		 x_dim, y_dim,
		 pop_frag,
		 addr_q,
		 pixel_q,
		 sw_q, tw_q, iw_q,
		 valid_q,
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
   input logic [31:0] x_dim;
   input logic [31:0] y_dim;

   
   input logic 	      pop_frag;
   
   output logic [31:0] addr_q;
   output logic [31:0] pixel_q;
   output logic [SW_W-1:0] sw_q;
   output logic [SW_W-1:0] tw_q;
   output logic [IW_W-1:0] iw_q;
   output logic        valid_q;
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
   logic 	t_save_tri, t_push_frag;

   localparam LG_OUTQ_D = 4;

   logic [LG_OUTQ_D:0] r_rdq_ptr, n_rdq_ptr;
   logic [LG_OUTQ_D:0] r_wrq_ptr, n_wrq_ptr;   
   
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
   logic [31:0] r_addrq [(1<<LG_OUTQ_D)-1:0];
   logic [31:0] r_pixelq [(1<<LG_OUTQ_D)-1:0];
   logic [SW_W-1:0] r_swq [(1<<LG_OUTQ_D)-1:0];
   logic [SW_W-1:0] r_twq [(1<<LG_OUTQ_D)-1:0];
   logic [IW_W-1:0] r_iwq [(1<<LG_OUTQ_D)-1:0];
   
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

   always_comb
     begin
	done = r_done;
	n_wrq_ptr = r_wrq_ptr;
	n_rdq_ptr = r_rdq_ptr;
	valid_q = r_wrq_ptr != r_rdq_ptr;
	addr_q = r_addrq[r_rdq_ptr[LG_OUTQ_D-1:0]];
	pixel_q = r_pixelq[r_rdq_ptr[LG_OUTQ_D-1:0]];
	sw_q = r_swq[r_rdq_ptr[LG_OUTQ_D-1:0]];
	tw_q = r_twq[r_rdq_ptr[LG_OUTQ_D-1:0]];
	iw_q = r_iwq[r_rdq_ptr[LG_OUTQ_D-1:0]];

	if(t_push_frag)
	  begin
	     n_wrq_ptr = r_wrq_ptr + 'd1;
	  end
	if(pop_frag)
	  begin
	     n_rdq_ptr = r_rdq_ptr + 'd1;
	  end
     end

   always_ff@(posedge clk)
     begin
	if(t_push_frag)
	  begin
             r_addrq[r_wrq_ptr[LG_OUTQ_D-1:0]] <= r_addr;
	     r_pixelq[r_wrq_ptr[LG_OUTQ_D-1:0]] <= 32'(r_z[DEPTH_W-1:DEPTH_FRAC_BITS]);
	     r_swq[r_wrq_ptr[LG_OUTQ_D-1:0]] <= r_sw;
	     r_twq[r_wrq_ptr[LG_OUTQ_D-1:0]] <= r_tw;
	     r_iwq[r_wrq_ptr[LG_OUTQ_D-1:0]] <= r_iw;
	  end
     end

   wire w_queue_full = (r_wrq_ptr[LG_OUTQ_D-1:0] == r_rdq_ptr[LG_OUTQ_D-1:0]) & 
	(r_wrq_ptr[LG_OUTQ_D] != r_rdq_ptr[LG_OUTQ_D]);
   wire w_queue_not_full = !w_queue_full;

   wire w_queue_empty = r_wrq_ptr == r_rdq_ptr;

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

	t_save_tri = 1'b0;
	t_push_frag = 1'b0;
	
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
	       if(w_queue_not_full)
		 begin
		    //test w0, w1, w2
		    t_push_frag = (r_w0[31] | r_w1[31] | r_w2[31])==1'b0;
		    
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

		      end // else: !if(r_x == r_x_max)
		    
		    if((r_y == w_y_max) & (r_x == r_x_max))
		      begin
			 n_state = DRAIN;
		      end
		 end // if (w_queue_not_full)
	    end
	  DRAIN:
	    begin
	       if(w_queue_empty)
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
	r_rdq_ptr <= rst ? 'd0 : n_rdq_ptr;
	r_wrq_ptr <= rst ? 'd0 : n_wrq_ptr;
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
     end // always_ff@ (posedge clk)
   
endmodule // rasterize

