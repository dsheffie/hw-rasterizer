UNAME_S = $(shell uname -s)

OBJ = top.o setup.o obj.o pipeline.o verilated.o

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

top.o: top.cc obj_dir/Vrasterize__ALL.a
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
