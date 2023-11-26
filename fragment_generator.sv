`include "rasterizer.vh"

module fragment_generator(clk,rst,start,
			  ymin,ymax,xmin,xmax,
			  l0_dx,l1_dx,l2_dx,
			  l0_dy,l1_dy,l2_dy,
			  w0_00,w1_00,w2_00,
			  pop_frag,
			  frag_val,
			  frag,
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
   
   logic 	      r_done, n_done;
   
   logic [`LG_FRAG_FIFO_SZ:0] r_credits, n_credits;
   localparam FRAG_FIFO_SZ = 1 << `LG_FRAG_FIFO_SZ;

   logic [`LG_FRAG_FIFO_SZ:0] r_fifo_head_ptr, n_fifo_head_ptr;
   logic [`LG_FRAG_FIFO_SZ:0] r_fifo_tail_ptr, n_fifo_tail_ptr;
   
   fragment_t r_frag_fifo[FRAG_FIFO_SZ-1:0];
   
      
   typedef enum logic [2:0] { IDLE = 0, GEN_W0 = 1, GEN_W1 = 2, GEN_W2 =3} state_t;
   state_t r_state, n_state;

   assign done = r_done;
   assign frag = r_frag_fifo[r_fifo_head_ptr[`LG_FRAG_FIFO_SZ-1:0]];
   assign frag_val = r_fifo_head_ptr != r_fifo_tail_ptr;
		    
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
	     r_fifo_head_ptr <= 'd0;
	     r_fifo_tail_ptr <= 'd0;
	     r_state <= IDLE;
	     r_credits <= 'd0;
	     r_done <= 1'b0;
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
	     r_fifo_head_ptr <= n_fifo_head_ptr;
	     r_fifo_tail_ptr <= n_fifo_tail_ptr;
	     r_state <= n_state;
	     r_credits <= n_credits;
	     r_done <= n_done;
	  end
     end // always_ff@ (posedge clk)


   logic t_start;
   always_comb
     begin
	n_fifo_tail_ptr = r_fifo_tail_ptr;
	n_fifo_head_ptr = r_fifo_head_ptr;

	if(t_start)
	  begin
	     n_fifo_tail_ptr = r_fifo_tail_ptr + 'd1;
	  end
	if(pop_frag)
	  begin
	     n_fifo_head_ptr = r_fifo_head_ptr + 'd1;
	  end
     end // always_comb

       
   wire [31:0] w_x = r_x + 'd1;

   always_ff@(negedge clk)
     begin
	if(t_start)
	  begin
	     $display("start x=%d, y=%d", r_x, r_y);
	  end
	//$display("r_state = %d", r_state);
     end

   logic [31:0] t_mul_srcA, t_mul_srcB;
   
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
	n_state = r_state;
	n_credits = r_credits;
	n_done = 1'b0;

	t_start = 1'b0;

	t_mul_srcA = r_w0;
	t_mul_srcB = r_l0_dy;
	
	
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
		    n_xmax = ymax;
		    n_l0_dx = l0_dx;
		    n_l1_dx = l1_dx;
		    n_l2_dx = l2_dx;
		    n_l0_dy = l0_dy;
		    n_l1_dy = l1_dy;
		    n_l2_dy = l2_dy;
		    n_w0_00 = w0_00;
		    n_w1_00 = w1_00;
		    n_w2_00 = w2_00;
		    
		    n_w0 = r_w0;
		    n_w1 = r_w1;
		    n_w2 = r_w2;
		    
		    n_state = GEN_W0;
		    n_credits = FRAG_FIFO_SZ;
		 end
	    end
	  GEN_W0:
	    begin
	       //check if there are enough credits available, if not - do nothing
	       if(r_credits != 'd0)
		 begin
		    t_start = 1'b1;
		    n_credits = pop_frag ? r_credits : (r_credits - 'd1);
		    n_state = GEN_W1;
		    t_mul_srcA = r_w0;
		    t_mul_srcB = r_l0_dy;
		 end // if (r_credits != 'd0)
	    end // case: GEN_W0
	  GEN_W1:
	    begin
	       n_state = GEN_W2;
	       n_credits = pop_frag ? r_credits + 'd1 : r_credits;
	       t_mul_srcA = r_w1;
	       t_mul_srcB = r_l1_dy;
	    end
	  GEN_W2:
	    begin
	       n_credits = pop_frag ? r_credits + 'd1 : r_credits;
	       t_mul_srcA = r_w2;
	       t_mul_srcB = r_l2_dy;
	       
	       if(w_x == r_xmax)
		 begin
		    $finish();
		    n_x = r_xmin;
		    n_y = r_y + 'd1;
		 end
	       else
		 begin
		    n_x = w_x;
		 end
	       
	       if(w_x == r_xmax && n_y == r_ymax)
		 begin
		    n_done = 1'b1;
		    n_state = IDLE;
		    $finish();
		 end
	       else
		 begin
		    n_state = GEN_W0;
		 end
	    end // case: GEN_W2
	  
	  default:
	    begin
	    end
	endcase // case (r_state)
     end // always_comb
   
   
   

endmodule // fragment_generator
