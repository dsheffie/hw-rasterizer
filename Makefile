UNAME_S = $(shell uname -s)

OBJ = obj_demo.o gfx.o hw_rast_verilated.o setup.o obj.o pipeline.o verilated.o

SV_SRC = rasterize.sv recip.sv

# render resolution (square).  256 fits the FPGA BRAM; bump for sim inspection,
# e.g. `make clean && make RES=512` (or just `make hires`).  Drives both the RTL
# framebuffer/depth BRAM sizing and the C++ image dimensions.
RES ?= 256

ifeq ($(UNAME_S),Linux)
	CXX = clang++-14 -flto
	MAKE = make
	VERILATOR_SRC = /home/dsheffie/local/share/verilator/include/verilated.cpp
	VERILATOR_FST = /home/dsheffie/local/share/verilator/include/verilated_fst_c.cpp
	VERILATOR_INC = /home/dsheffie/local/share/verilator/include
	VERILATOR_DPI_INC = /home/dsheffie/local/share/verilator/include/vltstd/
	VERILATOR = /home/dsheffie/local/bin/verilator
	SDL_CFLAGS = $(shell pkg-config --cflags sdl2)
	EXTRA_LD = -lcapstone -lboost_program_options  -lboost_serialization -lunwind $(shell pkg-config --libs sdl2)
endif

ifeq ($(UNAME_S),Darwin)
	CXX = clang++ -I/opt/local/include -flto
	VERILATOR_SRC = /Users/dsheffie/local/share/verilator/include/verilated.cpp
	VERILATOR_INC = /Users/dsheffie/local/share/verilator/include
	VERILATOR_FST = /Users/dsheffie/local/share/verilator/include/verilated_fst_c.cpp
	VERILATOR_DPI_INC = /Users/dsheffie/local/share/verilator/include/vltstd/
	VERILATOR = /Users/dsheffie/local/bin/verilator
	EXTRA_LD = -L/opt/local/lib -lboost_program_options-mt -lboost_serialization-mt -lcapstone -lSDL2
endif

OPT = -O3 -g -std=c++14 -fomit-frame-pointer
CXXFLAGS = -std=c++11 -g  $(OPT) -I$(VERILATOR_INC) -I$(VERILATOR_DPI_INC) $(SDL_CFLAGS) -DSCREEN_RES=$(RES) #-DLINUX_SYSCALL_EMULATION=1
LIBS =  $(EXTRA_LD) -lpthread

DEP = $(OBJ:.o=.d)

EXE = hw_rasterize

.PHONY : all clean

all: $(EXE)

$(EXE) : $(OBJ) obj_dir/Vrasterize__ALL.a recip_seed.hex
	$(CXX) $(CXXFLAGS) $(OBJ) obj_dir/*.o $(LIBS) -o $(EXE)

hw_rast_verilated.o: hw_rast_verilated.cc hw_rast.h obj_dir/Vrasterize__ALL.a
	$(CXX) -MMD $(CXXFLAGS) -Iobj_dir -c $<

verilated.o: $(VERILATOR_SRC)
	$(CXX) -MMD $(CXXFLAGS) -c $< 

%.o: %.cc
	$(CXX) -MMD $(CXXFLAGS) -c $< 

obj_dir/Vrasterize__ALL.a : $(SV_SRC)
	$(VERILATOR) --x-assign unique -cc rasterize.sv recip.sv --top-module rasterize +define+SCREEN_RES=$(RES)
	$(MAKE) OPT_FAST="-O3 -flto" -C obj_dir -f Vrasterize.mk

# convenience: clean rebuild at a larger resolution for human inspection
.PHONY: hires
hires:
	$(MAKE) clean
	$(MAKE) RES=512

# --- IRIS GL backend (sgi-demos libgl) ---
# The SGI display is 800x480 (XMAXSCREEN/YMAXSCREEN), bigger than the FPGA BRAM
# but free in the Verilated model, so this path builds the engine non-square.
SGI_INC = -Isgi-demos/libs/libgl -Isgi-demos/include/gl

obj_dir_demo/Vrasterize__ALL.a : $(SV_SRC)
	$(VERILATOR) --x-assign unique -cc rasterize.sv recip.sv --top-module rasterize --Mdir obj_dir_demo +define+SCREEN_WIDTH=800 +define+SCREEN_HEIGHT=480
	$(MAKE) OPT_FAST="-O3 -flto" -C obj_dir_demo -f Vrasterize.mk

# isolation test: feed rasterizer.h triangles directly, dump a PPM
.PHONY: irisgl_test
irisgl_test: irisgl_test_bin
	./irisgl_test_bin

irisgl_test_bin: rast_test.cc hw_rasterizer.cc gfx.cc hw_rast_verilated.cc setup.cc obj_dir_demo/Vrasterize__ALL.a recip_seed.hex verilated.o
	$(CXX) $(CXXFLAGS) $(SGI_INC) -Iobj_dir_demo rast_test.cc hw_rasterizer.cc gfx.cc hw_rast_verilated.cc setup.cc obj_dir_demo/*.o verilated.o -lpthread $(shell pkg-config --libs sdl2) -o irisgl_test_bin

# Run a real SGI/IRIS GL demo (ideas) on our engine.  We build the demo the
# repo's own way (its shim/K&R-C/GLES handling) with clang, then re-link it
# against OUR rasterizer: placing hw_rasterizer ahead of libgl.a means the
# linker resolves rasterizer_* from us and never pulls reference_rasterizer.o.
SGI       = sgi-demos
SGI_CC    = clang-14
SGI_BIN   = bin-linux-x86_64
GLES_DIR  = $(SGI)/libs/libgles
GLES_LINK = -L$(GLES_DIR)/lib-linux -lGLESv2 -lEGL -Wl,-rpath,$(GLES_DIR)/lib-linux

# Generic: `make <demo>_hw` builds any sgi-demos/demos/<demo> against our engine
# (e.g. make ideas_hw, make logo_hw, make jello_hw).  Same recipe for every
# demo: build it the repo's way, then re-link with our rasterizer ahead of
# libgl.a.  `make ideas` is kept as a friendly alias for the original demo.
.PHONY: ideas
ideas: ideas_hw
	@echo "built ideas_hw -- run: ./ideas_hw"

%_hw: hw_rasterizer.cc gfx.cc hw_rast_verilated.cc setup.cc obj_dir_demo/Vrasterize__ALL.a recip_seed.hex verilated.o
	$(MAKE) native -C $(SGI)/demos/$* CC="$(SGI_CC)"
	$(CXX) $(CXXFLAGS) $(SGI_INC) -Iobj_dir_demo \
	  hw_rasterizer.cc gfx.cc hw_rast_verilated.cc setup.cc \
	  $(SGI)/demos/$*/$(SGI_BIN)/*.o \
	  $(SGI)/libs/libdemo/$(SGI_BIN)/libdemo.a \
	  obj_dir_demo/*.o verilated.o \
	  $(SGI)/libs/libgl/$(SGI_BIN)/libgl.a \
	  $(shell sdl2-config --libs) $(GLES_LINK) -lm -lpthread -o $@

# standalone reciprocal module + bit-exact testbench
.PHONY: recip_test
recip_test: recip_tb recip_seed.hex
	./recip_tb

# seed ROM: entry i = round(65536 / (1 + (i+0.5)/256)), matches init_recip_seed
recip_seed.hex:
	python3 -c "import math; print('\n'.join('%x'%int(math.floor(65536.0/(1.0+(i+0.5)/256.0)+0.5)) for i in range(256)))" > $@

obj_dir_recip/Vrecip__ALL.a : recip.sv
	$(VERILATOR) --x-assign unique -cc recip.sv --Mdir obj_dir_recip
	$(MAKE) OPT_FAST="-O3 -flto" -C obj_dir_recip -f Vrecip.mk

recip_tb: recip_tb.cc obj_dir_recip/Vrecip__ALL.a verilated.o
	$(CXX) $(CXXFLAGS) -Iobj_dir_recip recip_tb.cc obj_dir_recip/*.o verilated.o -lpthread -o recip_tb

-include $(DEP)

clean:
	rm -rf $(EXE) $(OBJ) $(DEP) obj_dir obj_dir_recip recip_tb
