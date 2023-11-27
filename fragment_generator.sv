`include "rasterizer.vh"

module fragment_generator(clk,rst,start,
			  ymin,ymax,xmin,xmax,
			  l0_dx,l1_dx,l2_dx,
			  l0_dy,l1_dy,l2_dy,
			  w0_00,w1_00,w2_00,
			  pop_frag,
			  frag_val,
			  frag,

			  x_out,
			  y_out,
			  w0_out,
			  w1_out,
			  w2_out,
			  
			  done
			  );
   input logic clk;
   input logic rst;
   input logic start;
   
   input logic [31:0] ymin;
   input logic [31:0] ymax;
   input logic [31:0] xmin;
   input logic [31:0] xmax;

   input logic [31:0] l0_dx;
   input logic [31:0] l1_dx;
   input logic [31:0] l2_dx;
   input logic [31:0] l0_dy;
   input logic [31:0] l1_dy;
   input logic [31:0] l2_dy;         
   input logic [31:0] w0_00;
   input logic [31:0] w1_00;
   input logic [31:0] w2_00;

   input logic pop_frag;
   output logic       frag_val;
   output fragment_t frag;


   output logic [31:0] x_out;
   output logic [31:0] y_out;

   output logic [31:0] w0_out;
   output logic [31:0] w1_out;
   output logic [31:0] w2_out;
   
   output logic done;
   
   
   logic [31:0]       r_ymin, r_ymax, r_xmin, r_xmax;
   logic [31:0]       n_ymin, n_ymax, n_xmin, n_xmax;

   logic [31:0]       r_l0_dx, n_l0_dx;
   logic [31:0]       r_l1_dx, n_l1_dx;
   logic [31:0]       r_l2_dx, n_l2_dx;
   logic [31:0]       r_l0_dy, n_l0_dy;
   logic [31:0]       r_l1_dy, n_l1_dy;
   logic [31:0]       r_l2_dy, n_l2_dy;
   logic [31:0]       r_w0_00, n_w0_00;
   logic [31:0]       r_w1_00, n_w1_00;
   logic [31:0]       r_w2_00, n_w2_00;
   
   logic [31:0]       r_y, n_y, r_x, n_x;
   
   logic [31:0]       r_w0, r_w1, r_w2;
   logic [31:0]       n_w0, n_w1, n_w2;

   logic [31:0]       n_last_x, r_last_x;
   logic [31:0]       n_last_y, r_last_y;
   
   logic [31:0]       n_last_w0, r_last_w0;
   logic [31:0]       n_last_w1, r_last_w1;
   logic [31:0]       n_last_w2, r_last_w2;   
   
   
   logic 	      r_done, n_done;
   
   localparam FRAG_FIFO_SZ = 1 << `LG_FRAG_FIFO_SZ;

   logic [`LG_FRAG_FIFO_SZ:0] r_fifo_head_ptr, n_fifo_head_ptr;
   logic [`LG_FRAG_FIFO_SZ:0] r_fifo_tail_ptr, n_fifo_tail_ptr;

   logic [`LG_FRAG_FIFO_SZ:0] r_credits, n_credits;
   fragment_t r_frag_fifo[FRAG_FIFO_SZ-1:0];
   
      
   typedef enum logic [3:0] { INVALID=0, IDLE, INIT_FRAG, 
			      GEN_W0, GEN_W1, GEN_W2,
			      INCR_Y_W0, INCR_Y_W1, INCR_Y_W2,
			      DELAY_0, DELAY_1, 
			      DONE_DRAIN, ASSERT_DONE} state_t;
   state_t r_state, n_state;

   assign done = r_done;
   assign frag = r_frag_fifo[r_fifo_head_ptr[`LG_FRAG_FIFO_SZ-1:0]];
   assign frag_val = r_fifo_head_ptr != r_fifo_tail_ptr;

   assign x_out = r_frag_fifo[r_fifo_head_ptr[`LG_FRAG_FIFO_SZ-1:0]].x;
   assign y_out = r_frag_fifo[r_fifo_head_ptr[`LG_FRAG_FIFO_SZ-1:0]].y;
   assign w0_out = r_frag_fifo[r_fifo_head_ptr[`LG_FRAG_FIFO_SZ-1:0]].w0;
   assign w1_out = r_frag_fifo[r_fifo_head_ptr[`LG_FRAG_FIFO_SZ-1:0]].w1;
   assign w2_out = r_frag_fifo[r_fifo_head_ptr[`LG_FRAG_FIFO_SZ-1:0]].w2;      

   wire 	w_fifo_empty = r_fifo_head_ptr == r_fifo_tail_ptr;
   wire 	w_fifo_full = (r_fifo_head_ptr[`LG_FRAG_FIFO_SZ-1:0] == r_fifo_tail_ptr[`LG_FRAG_FIFO_SZ-1:0]) && 
		(r_fifo_head_ptr[`LG_FRAG_FIFO_SZ] != r_fifo_tail_ptr[`LG_FRAG_FIFO_SZ]);
   
   state_t r_last_states[`FP_ADD_LAT:0];
   logic [31:0] t_add_srcA, t_add_srcB;
   logic 	t_add_start, t_add_sub;
   wire [31:0] 	w_adder_out;
   
   always_ff@(posedge clk)
     begin
	if(rst)
	  begin
	     for(integer i = 0; i <= (`FP_ADD_LAT); i=i+1)
	       begin
		  r_last_states[i] <= INVALID;
	       end
	  end
	else
	  begin
	     r_last_states[0] <= t_add_start ? r_state : INVALID;
	     for(integer i = 1; i <= (`FP_ADD_LAT); i=i+1)
	       begin
		  r_last_states[i] <= r_last_states[i-1];
	       end	     
	  end
     end

   wire 	 w_w0_not_negative = (r_frag.w0[30:0] == 'd0) || (r_frag.w0[31] == 1'b0);
   wire 	 w_w1_not_negative = (r_frag.w1[30:0] == 'd0) || (r_frag.w1[31] == 1'b0);
   wire 	 w_w2_not_negative = (r_frag.w2[30:0] == 'd0) || (r_frag.w2[31] == 1'b0);
   wire 	 w_point_in_tri = w_w0_not_negative && w_w1_not_negative && w_w2_not_negative;
   
   always_ff@(posedge clk)
     begin
	if(rst)
	  begin
	     r_ymin <= 'd0;
	     r_ymax <= 'd0;
	     r_xmin <= 'd0;
	     r_xmax <= 'd0;
             r_l0_dx <= 'd0;
             r_l1_dx <= 'd0;
             r_l2_dx <= 'd0;
             r_l0_dy <= 'd0;
             r_l1_dy <= 'd0;
             r_l2_dy <= 'd0;
             r_w0_00 <= 'd0;
             r_w1_00 <= 'd0;
             r_w2_00 <= 'd0;
	     r_w0 <= 'd0;
	     r_w1 <= 'd0;
	     r_w2 <= 'd0;
	     r_y <= 'd0;
	     r_x <= 'd0;
	     r_last_x <= 'd0;
	     r_last_y <= 'd0;
	     r_last_w0 <= 'd0;
	     r_last_w1 <= 'd0;
	     r_last_w2 <= 'd0;
	     r_fifo_head_ptr <= 'd0;
	     r_fifo_tail_ptr <= 'd0;
	     r_state <= IDLE;
	     r_done <= 1'b0;
	     r_credits <= FRAG_FIFO_SZ;
	  end
	else
	  begin
	     r_ymin <= n_ymin;
	     r_ymax <= n_ymax;
	     r_xmin <= n_xmin;
	     r_xmax <= n_xmax;
             r_l0_dx <= n_l0_dx;
             r_l1_dx <= n_l1_dx;
             r_l2_dx <= n_l2_dx;
             r_l0_dy <= n_l0_dy;
             r_l1_dy <= n_l1_dy;
             r_l2_dy <= n_l2_dy;
             r_w0_00 <= n_w0_00;
             r_w1_00 <= n_w1_00;
             r_w2_00 <= n_w2_00;
	     r_w0 <= n_w0;
	     r_w1 <= n_w1;
	     r_w2 <= n_w2;	     
	     
	     r_y <= n_y;
	     r_x <= n_x;
	     r_last_x <= n_last_x;
	     r_last_y <= n_last_y;
	     r_last_w0 <= n_last_w0;
	     r_last_w1 <= n_last_w1;
	     r_last_w2 <= n_last_w2;

	     r_fifo_head_ptr <= n_fifo_head_ptr;
	     r_fifo_tail_ptr <= n_fifo_tail_ptr;
	     r_state <= n_state;
	     r_done <= n_done;
	     r_credits <= n_credits;
	  end
     end // always_ff@ (posedge clk)


   logic r_push_fifo, t_push_fifo, t_frag_done;
   fragment_t r_frag, t_frag;

   always_ff@(posedge clk)
     begin
	r_push_fifo <= rst ? 1'b0 : t_frag_done;
	r_frag <= t_frag;
     end
   
   always_comb
     begin
	n_fifo_tail_ptr = r_fifo_tail_ptr;
	n_fifo_head_ptr = r_fifo_head_ptr;
	n_credits = r_credits;
	
	t_push_fifo = r_push_fifo ? w_point_in_tri : 1'b0;

	if(t_push_fifo && !pop_frag)
	  begin
	     n_credits = r_credits - 'd1;
	  end
	else if(!t_push_fifo && pop_frag)
	  begin
	     n_credits = r_credits + 'd1;
	  end
	
	if(t_push_fifo)
	  begin
	     n_fifo_tail_ptr = r_fifo_tail_ptr + 'd1;
	  end
	if(pop_frag)
	  begin
	     n_fifo_head_ptr = r_fifo_head_ptr + 'd1;
	  end
     end // always_comb

   always_ff@(posedge clk)
     begin
	if(t_push_fifo)
	  begin
	     r_frag_fifo[r_fifo_tail_ptr[`LG_FRAG_FIFO_SZ-1:0]] <= r_frag;
	  end
     end
   
       
   wire [31:0] w_x = r_x + 'd1;

   always_ff@(negedge clk)
     begin
	//$display("state = %d", r_state);
	if(t_push_fifo)
	  begin
	     $display("cycle %d : done with frag for x=%d, y=%d, t_frag.w1 = %x", 
		      r_cycle, r_last_x, r_last_y, t_frag.w1);
	  end
	//$display("adder out %x", w_adder_out);
     end

   logic [31:0] r_cycle;
   always_ff@(posedge clk)
     begin
	r_cycle <= (rst) ? 'd0 : (r_cycle + 'd1);
     end
   
   
   always_comb
     begin
	n_ymin = r_ymin;
	n_ymax = r_ymax;
	n_xmin = r_xmin;
	n_xmax = r_xmax;
	n_l0_dx = r_l0_dx;
	n_l1_dx = r_l1_dx;
	n_l2_dx = r_l2_dx;
	n_l0_dy = r_l0_dy;
	n_l1_dy = r_l1_dy;
	n_l2_dy = r_l2_dy;
	n_w0_00 = r_w0_00;
	n_w1_00 = r_w1_00;
	n_w2_00 = r_w2_00;
	n_w0 = r_w0;
	n_w1 = r_w1;
	n_w2 = r_w2;

	n_y = r_y;
	n_x = r_x;

	n_last_x = r_last_x;
	n_last_y = r_last_y;	
	n_last_w0 = r_last_w0;
	n_last_w1 = r_last_w1;
	n_last_w2 = r_last_w2;		
	
	
	n_state = r_state;       
	n_done = 1'b0;

	//t_push_fifo = 1'b0;
	t_frag_done = 1'b0;
	
	t_add_srcA = r_w0;
	t_add_srcB = r_l0_dy;
	t_add_start = 1'b0;
	t_add_sub = 1'b0;

		
	if(r_last_states[`FP_ADD_LAT-1] == GEN_W0)
	  begin
	     n_w0 = w_adder_out;
	     n_last_w0 = w_adder_out;
	  end
	else if(r_last_states[`FP_ADD_LAT-1] == GEN_W1)
	  begin
	     n_w1 = w_adder_out;
	     n_last_w1 = w_adder_out;
	  end
	else if(r_last_states[`FP_ADD_LAT-1] == GEN_W2)
	  begin
	     n_w2 = w_adder_out;
	     n_last_w2 = w_adder_out;
	     t_frag_done = 1'b1;
	  end
	else if(r_last_states[`FP_ADD_LAT-1] == INCR_Y_W0)
	  begin
	     $display("INCR_Y_W0 clobbers n_w0 at cycle %d, out %x, state = %d", r_cycle,  w_adder_out, r_state);	     
	     n_w0_00 = w_adder_out;
	     n_w0 = w_adder_out;
	     n_last_w0 =  w_adder_out;
	  end
	else if(r_last_states[`FP_ADD_LAT-1] == INCR_Y_W1)
	  begin
	     $display("INCR_Y_W1 clobbers n_w1 at cycle %d, out %x, state %d", r_cycle, w_adder_out, r_state);
	     n_w1_00 = w_adder_out;
	     n_w1 = w_adder_out;
	     n_last_w1 = w_adder_out;
	  end
	else if(r_last_states[`FP_ADD_LAT-1] == INCR_Y_W2)
	  begin
	     $display("INCR_Y_W2 clobbers n_w2 at cycle %d, out %x, state = %d", r_cycle, w_adder_out, r_state);
	     n_w2_00 = w_adder_out;
	     n_w2 = w_adder_out;
	     n_last_w2 = w_adder_out;	     
	  end


	t_frag.w0 = n_last_w0;
	t_frag.w1 = n_last_w1;
	t_frag.w2 = n_last_w2;
	t_frag.x = n_last_x;
	t_frag.y = n_last_y;

	
	case(r_state)
	  IDLE:
	    begin
	       if(start)
		 begin
		    n_y = ymin;
		    n_x = xmin;
		    n_ymin = ymin;
		    n_ymax = ymax;
		    n_xmin = xmin;
		    n_xmax = xmax;
		    n_l0_dx = l0_dx;
		    n_l1_dx = l1_dx;
		    n_l2_dx = l2_dx;
		    n_l0_dy = l0_dy;
		    n_l1_dy = l1_dy;
		    n_l2_dy = l2_dy;
		    n_w0_00 = w0_00;
		    n_w1_00 = w1_00;
		    n_w2_00 = w2_00;
		    
		    n_w0 = w0_00;
		    n_w1 = w1_00;
		    n_w2 = w2_00;
		    
		    n_last_w0 = w0_00;
		    n_last_w1 = w1_00;
		    n_last_w2 = w2_00;

		    n_last_x = xmin;
		    n_last_y = ymin;

		    n_state = INIT_FRAG;
		 end
	    end // case: IDLE
	  INIT_FRAG:
	    begin
	       t_frag_done = 1'b1;
	       if(r_x == r_xmax)
		 begin
		    n_state = INCR_Y_W0;
		    n_y = r_y + 'd1;
		 end
	       else
		 begin
		    n_x = r_x + 'd1;
		    n_state = GEN_W0;
		 end
	    end
	  GEN_W0:
	    begin
	       if(r_credits > 'd1)
		 begin
		    n_state = GEN_W1;
		    t_add_srcA = n_w0;
		    t_add_srcB = r_l0_dy;
		    t_add_start = 1'b1;
		 end // if (r_credits != 'd0)
	    end // case: GEN_W0
	  GEN_W1:
	    begin
	       n_state = GEN_W2;
	       t_add_srcA = n_w1;
	       t_add_srcB = r_l1_dy;
	       t_add_start = 1'b1;	       
	    end
	  GEN_W2:
	    begin
	       t_add_srcA = n_w2;
	       t_add_srcB = r_l2_dy;
	       t_add_start = 1'b1;

	       n_last_x = r_x;
	       n_last_y = r_y;

	       if(r_x == r_xmax && n_y == r_ymax)
                begin
                   n_state = DONE_DRAIN;
                end
	       else if(r_x == r_xmax)
		 begin
		    n_state = DELAY_0;
		    n_x = r_xmin;
		    n_y = r_y + 'd1;
		 end
	       else
		 begin
		    n_x = w_x;
		    n_state = GEN_W0;		    
		 end
	       
	    end // case: GEN_W2
	  DELAY_0:
	    begin
	       if(r_last_states[`FP_ADD_LAT-1] == GEN_W2)
		 begin
		    n_state = INCR_Y_W0;
		 end
	    end
	  INCR_Y_W0:
	    begin
	       t_add_srcA = r_w0_00;
	       t_add_srcB = r_l0_dx;
	       t_add_start = 1'b1;	    
	       t_add_sub = 1'b1;	          
	       n_state = INCR_Y_W1;
	    end
	  INCR_Y_W1:
	    begin
	       t_add_srcA = r_w1_00;
	       t_add_srcB = r_l1_dx;
	       t_add_start = 1'b1;
	       t_add_sub = 1'b1;	       	       
	       n_state = INCR_Y_W2;
	    end
	  INCR_Y_W2:
	    begin
	       t_add_srcA = r_w2_00;
	       t_add_srcB = r_l2_dx;
	       t_add_start = 1'b1;
	       t_add_sub = 1'b1;	       
	       n_state = DELAY_1;
	    end
	  DELAY_1:
	    begin
	       n_last_x = r_x;
	       n_last_y = r_y;
	       if(r_last_states[`FP_ADD_LAT-1] == INCR_Y_W2)
		 begin
		    t_frag_done = 1'b1;
		    if(r_x == r_xmax)
		      begin
			 n_state = INCR_Y_W0;
			 n_y = r_y + 'd1;
		      end
		    else
		      begin
			 n_state = GEN_W0;
			 n_x = r_x + 'd1;
		      end
		 end
	    end // case: DELAY_1
	  DONE_DRAIN:
	    begin
	       if(r_last_states[`FP_ADD_LAT-1] == GEN_W2)
		 begin
		    n_state = ASSERT_DONE;
		 end
	    end
	  ASSERT_DONE:
	    begin
	       if(w_fifo_empty)
		 begin
		    n_done = 1'b1;
		    n_state = IDLE;
		 end
	    end
	  default:
	    begin
	    end
	endcase // case (r_state)
     end // always_comb
   
   
   fp_add adder (.clk(clk), 
		 .y(w_adder_out), 
		 .a(t_add_srcA), 
		 .b(t_add_srcB), 
		 .en(t_add_start), 
		 .sub(t_add_sub) );

endmodule // fragment_generator
