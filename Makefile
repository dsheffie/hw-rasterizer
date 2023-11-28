UNAME_S = $(shell uname -s)

OBJ = verilated.o top.o 
SV_SRC = fragment_generator.sv pineda.sv fp_add.sv count_leading_zeros.sv

CXX = clang++-12 -flto #-DUSE_SDL
MAKE = make
VERILATOR_SRC = /home/dsheffie/local/share/verilator/include/verilated.cpp
VERILATOR_VCD = /home/dsheffie/local/share/verilator/include/verilated_vcd_c.cpp
VERILATOR_INC = /home/dsheffie/local/share/verilator/include
VERILATOR_DPI_INC = /home/dsheffie/local/share/verilator/include/vltstd/
VERILATOR = /home/dsheffie/local/bin/verilator
EXTRA_LD = #-lcapstone -lboost_program_options  -lboost_serialization -lSDL2


OPT = -O3 -g -std=c++14 -fomit-frame-pointer
CXXFLAGS = -std=c++11 -g  $(OPT) -I$(VERILATOR_INC) -I$(VERILATOR_DPI_INC)
LIBS =  $(EXTRA_LD) -lpthread

DEP = $(OBJ:.o=.d)

EXE = frag-gen

.PHONY : all clean

all: $(EXE)

$(EXE) : $(OBJ) obj_dir/Vfragment_generator__ALL.a
	$(CXX) $(CXXFLAGS) $(OBJ) obj_dir/*.o $(LIBS) -o $(EXE)

top.o: top.cc obj_dir/Vfragment_generator__ALL.a
	$(CXX) -MMD $(CXXFLAGS) -Iobj_dir -c $< 

verilated.o: $(VERILATOR_SRC)
	$(CXX) -MMD $(CXXFLAGS) -c $< 

verilated_vcd_c.o: $(VERILATOR_VCD)
	$(CXX) -MMD $(CXXFLAGS) -c $< 

%.o: %.cc
	$(CXX) -MMD $(CXXFLAGS) -c $< 

obj_dir/Vfragment_generator__ALL.a : $(SV_SRC)
	$(VERILATOR) -cc fragment_generator.sv
	$(MAKE) OPT_FAST="-O3 -flto" -C obj_dir -f Vfragment_generator.mk


-include $(DEP)



clean:
	rm -rf $(EXE) $(OBJ) $(DEP) obj_dir
