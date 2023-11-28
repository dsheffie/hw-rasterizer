`include "rasterizer.vh"

module fragment_generator(clk,rst,
			  start,
			  ymin,ymax,xmin,xmax,
			  l0_dx,l1_dx,l2_dx,
			  l0_dy,l1_dy,l2_dy,
			  w0_00,w1_00,w2_00,
			  recip_area,
			  v0_raster_z,
			  v1_raster_z,
			  v2_raster_z,
			  pop_frag,
			  frag_val,
			  frag,

			  x_out,
			  y_out,
			  w0_out,
			  w1_out,
			  w2_out,
			  ready,
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

   input logic [31:0] recip_area;
   input logic [31:0] v0_raster_z;
   input logic [31:0] v1_raster_z;
   input logic [31:0] v2_raster_z;
   
   input logic pop_frag;
   output logic       frag_val;
   output fragment_t frag;


   output logic [31:0] x_out;
   output logic [31:0] y_out;

   output logic [31:0] w0_out;
   output logic [31:0] w1_out;
   output logic [31:0] w2_out;
   
   output logic        ready;
   output logic        done;


   pineda p0 (.clk(clk),
	      .rst(rst),
	      .start(start),
	      .ymin(ymin),
	      .ymax(ymax),
	      .xmin(xmin),
	      .xmax(xmax),
	      .l0_dx(l0_dx),
	      .l1_dx(l1_dx),
	      .l2_dx(l2_dx),
	      .l0_dy(l0_dy),
	      .l1_dy(l1_dy),
	      .l2_dy(l2_dy),
	      .w0_00(w0_00),
	      .w1_00(w1_00),
	      .w2_00(w2_00),
	      .pop_frag(pop_frag),
	      .frag_val(frag_val),
	      .frag(frag),
	      .x_out(x_out),
	      .y_out(y_out),
	      .w0_out(w0_out),
	      .w1_out(w1_out),
	      .w2_out(w2_out),
	      .ready(ready),
	      .done(done)
	      );
   
	      
   
endmodule // fragment_generator
