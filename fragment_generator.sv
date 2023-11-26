`include "rasterizer.vh"

module fragment_generator(clk,rst,start,
			  ymin,ymax,xmin,xmax,
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

   input logic pop_frag;
   output logic       frag_val;
   output fragment_t frag;
   output logic done;
   
   
   logic [31:0]       r_ymin, r_ymax, r_xmin, r_xmax;
   logic [31:0]       n_ymin, n_ymax, n_xmin, n_xmax;
   logic [31:0]       r_y, n_y, r_x, n_x;
   logic 	      r_done, n_done;
   
   logic [`LG_FRAG_FIFO_SZ:0] r_credits, n_credits;
   localparam FRAG_FIFO_SZ = 1 << `LG_FRAG_FIFO_SZ;

   logic [`LG_FRAG_FIFO_SZ:0] r_fifo_head_ptr, n_fifo_head_ptr;
   logic [`LG_FRAG_FIFO_SZ:0] r_fifo_tail_ptr, n_fifo_tail_ptr;
   
   fragment_t r_frag_fifo[FRAG_FIFO_SZ-1:0];
   
      
   typedef enum logic [2:0] { IDLE = 0, RUN} state_t;
   state_t r_state, n_state;

   assign done = r_done;
   assign frag = r_frag_fifo[r_fifo_head_ptr[`LG_FRAG_FIFO_SZ-1:0]];
   
   always_ff@(posedge clk)
     begin
	if(rst)
	  begin
	     r_ymin <= 'd0;
	     r_ymax <= 'd0;
	     r_xmin <= 'd0;
	     r_xmax <= 'd0;
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
   
   always_comb
     begin
	n_ymin = r_ymin;
	n_ymax = r_ymax;
	n_xmin = r_xmin;
	n_xmax = r_xmax;
	n_y = r_y;
	n_x = r_x;
	n_state = r_state;
	n_credits = r_credits;
	n_done = 1'b0;

	t_start = 1'b0;
	
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
		    n_state = RUN;
		    n_credits = FRAG_FIFO_SZ;
		 end
	    end
	  RUN:
	    begin
	       //check if there are enough credits available, if not - do nothing
	       if(r_credits != 'd0)
		 begin
		    t_start = 1'b1;
		    n_credits = pop_frag ? r_credits : (r_credits - 'd1);
		    if(w_x == r_xmax)
		      begin
			 n_x = r_xmin;
			 n_y = r_y + 'd1;
		      end
		    else
		      begin
			 n_x = w_x;
		      end

		    if(r_x == r_xmax && r_y == r_ymax)
		      begin
			 n_done = 1'b1;
			 n_state = IDLE;
			 $finish();
		      end
		 end // if (r_credits != 'd0)
	       else
		 begin
		    $display("stalled due to lack of credits");
		 end
	    end
	  default:
	    begin
	    end
	endcase // case (r_state)
     end // always_comb
   
   
   

endmodule // fragment_generator
