`include "rasterizer.vh"

module zcalc(clk,
	     rst,
	     load,
	     recip_area,
	     v0_raster_z,
	     v1_raster_z,
	     v2_raster_z,
	     frag_in,
	     frag_in_val,
	     pop_frag_in,
	     frag_out,
	     frag_out_val,
	     pop_frag_out);

   input logic clk;
   input logic rst;
   input logic load;
   input logic [31:0] recip_area;
   input logic [31:0] v0_raster_z;
   input logic [31:0] v1_raster_z;
   input logic [31:0] v2_raster_z;   
   
   fragment_t frag_in;
   output logic pop_frag_in;
   fragment_t frag_out;
   output logic frag_out_val;
   input logic 	pop_frag_out;

   logic [31:0] r_recip_area, r_v0_raster_z, r_v1_raster_z, r_v2_raster_z;

   always_ff@(posedge clk)
     begin
	if(rst)
	  begin
	     r_recip_area <= 32'd0;
	     r_v0_raster_z <= 32'd0;
	     r_v1_raster_z <= 32'd0;
	     r_v2_raster_z <= 32'd0;	     
	  end
	else if(load)
	  begin
	     r_recip_area <= recip_area;
	     r_v0_raster_z <= v0_raster_z;
	     r_v1_raster_z <= v1_raster_z;
	     r_v2_raster_z <= v2_raster_z;	     
	  end
     end // always_ff@ (posedge clk)

	      
   //w0 *= recip_area;
   //w1 *= recip_area;
   //w2 *= recip_area;
   //float z = 1.0f / (v0Raster.z * w0 + v1Raster.z * w1 + v2Raster.z * w2);
   

   

endmodule
