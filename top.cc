#include <cstdint>
#include <cassert>
#include <iostream>
#include <fstream>
#include "Vrasterize.h"

const int32_t imageWidth = 512;
const int32_t imageHeight = 512;

struct pixel_t {
  uint8_t b;
  uint8_t g;
  uint8_t r;
  uint8_t a;
  pixel_t(uint8_t b, uint8_t g, uint8_t r, uint8_t a) : b(b), g(g), r(r), a(a) {}
};

typedef uint8_t Rgb[3];

struct vertex2d {
  int32_t x;
  int32_t y;
  vertex2d(int32_t x, int32_t y) :
    x(x), y(y) {}
};

std::ostream &operator<<(std::ostream &out, const vertex2d &v) {
  out << "(" << v.x << "," << v.y << ")";
  return out;
}

static int32_t cross(const vertex2d &a, const vertex2d &b, const vertex2d &p) {
  vertex2d ab(b.x-a.x, b.y-a.y);
  vertex2d ap(p.x-b.x, p.y-b.y);
  //std::cout << a << "," << b << "," << p << "," << x << "\n";
  return (ab.x * ap.y) - (ab.y * ap.x);
}

static int w_int[3] = {0};
static int w_p = 0;

int crossp(int x0, int y0,
	   int x1, int y1,
	   int x2, int y2) {
  vertex2d v0(x0, y0);
  vertex2d v1(x1, y1);
  vertex2d v2(x2, y2);
  int x = cross(v0, v1, v2);
  std::cout << w_p << "," << x << "\n";
  w_int[w_p++] = x;
  return x;
}

  // for(int32_t y = y_min; y <= y_max; y++) {
        
  //   int y_pv2 = y_pv2_;
  //   int y_pv1 = y_pv1_;
  //   int y_pv0 = y_pv0_;
    
  //   for(int32_t x = x_min; x <= x_max; x++) {
  //     int32_t w0 = y_pv2 + b0;
  //     int32_t w1 = y_pv1 + b1;
  //     int32_t w2 = y_pv0 + b2;
  //     printf("%d %d %d %d %d\n", x, y, w0, w1, w2);
  //     if(w0 >= 0 and w1 >= 0 and w2 >= 0) {
  // 	pxls[y * imageWidth + x] = c;
  // 	n_pxls++;
  //     }
      
  //     y_pv2 -= v2v1.y;
  //     y_pv1 -= v0v2.y;
  //     y_pv0 -= v1v0.y;      
  //   }
  //   //printf("incr y, step -%d, old %d\n", v1v0.x, y_pv0_);
  //   y_pv2_ += v2v1.x;
  //   y_pv1_ += v0v2.x;
  //   y_pv0_ += v1v0.x;
  // }

static int n_pix = 0;

void check_frag(int x, int y, int w0, int w1, int w2) {
  printf("check x=%d, y = %d, w0 = %d, w1 = %d, w2 = %d\n",
	 x, y, w0, w1, w2);
  //if(w0 >= 0 and w1 >= 0 and w2 >= 0) {
  //printf("draw pixel at %d, %d\n", x, y);
  //}

  n_pix++;
  //if(n_pix == 3) {
  //    exit(-1);
  //}
}


static inline void tick(Vrasterize *tb) {
  tb->clk = (~tb->clk) & 1;   
  tb->eval();
  tb->clk = (~tb->clk) & 1;
  tb->eval();    
}

int main(int argc, char *argv[]) {
  Vrasterize *tb = nullptr;
  const std::unique_ptr<VerilatedContext> contextp{new VerilatedContext};
  contextp->commandArgs(argc, argv);


  
  tb = new Vrasterize;
  tb->clk = 0;
  tb->rst = 1;
  tb->go  = 0;

  tb->v0_x = 50;
  tb->v0_y = 50;
  
  tb->v1_x = 100;
  tb->v1_y = 25;
  
  tb->v2_x = 50;
  tb->v2_y = 100;
  
  tb->x_dim = imageWidth;
  tb->y_dim = imageHeight;
  tick(tb);
  tb->rst = 0;
  tick(tb);
  tb->go = 1;
  tick(tb);
  tb->go = 0;

  int w = imageWidth;
  int h = imageHeight;
  Rgb *framebuffer = new Rgb[w * h];
  memset(framebuffer, 0x0, w * h * 3);
  bool done = false;
  while(not(done)) {
    tb->clk = (~tb->clk) & 1;
    tb->pop_frag = 0;          
    tb->eval();
    if(tb->done) {
      done = true;
    }
    if(tb->valid_q) {
      //printf("got valid frag, addr %d\n", tb->addr_q);
      framebuffer[tb->addr_q][0] = 255;
      tb->pop_frag = 1;      
    }
    tb->clk = (~tb->clk) & 1;   
    tb->eval();    
  }

  std::cout << "all done, trying to write image\n";
  std::ofstream ofs;
  ofs.open("raster2d.ppm");
  ofs << "P6\n" << w << " " << h << "\n255\n";
  ofs.write((char*)framebuffer, w * h * 3);
  ofs.close();

  delete [] framebuffer;
  
  
  return 0;
}

				  
