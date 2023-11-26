`ifndef __rasterizerhdr__
 `define __rasterizerhdr__

 `define LG_FRAG_FIFO_SZ 4
 `define FP_ADD_LAT 2

typedef struct packed {
   logic [31:0] w0;
   logic [31:0] w1;
   logic [31:0] w2;
   logic [31:0] x;
   logic [31:0] y;
} fragment_t;
 
`endif
