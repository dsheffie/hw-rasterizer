# Ultra96-v2 standalone hw-rasterizer: henry's PS8 config + rast_axi @ 0xA0050000
# usage: vivado -mode batch -source fpga/build.tcl [-tclargs <project dir>]
# -> <project dir>/rast96.runs/impl_1/rast96_wrapper.bit
set here [file dirname [file normalize [info script]]]
set src [file dirname $here]
# project dir: first -tclargs argument, default ~/fpga/ultra96v2-rast
set proj [expr {[llength $argv] > 0 ? [lindex $argv 0] : "$::env(HOME)/fpga/ultra96v2-rast"}]
create_project rast96 $proj -part xczu3eg-sbva484-1-e -force
add_files [list $src/rast_axi.v $src/rasterize.sv $src/recip.sv]
set_property verilog_define {SCREEN_WIDTH=320 SCREEN_HEIGHT=200} [get_filesets sources_1]
add_files -fileset constrs_1 $here/ultra96v2_oob.xdc
update_compile_order -fileset sources_1
source $here/rast_bd.tcl
set bd [get_files rast96.bd]
set_property synth_checkpoint_mode None $bd
validate_bd_design
save_bd_design
generate_target all $bd
add_files -norecurse [make_wrapper -files $bd -top]
set_property top rast96_wrapper [get_filesets sources_1]
update_compile_order -fileset sources_1
launch_runs impl_1 -to_step write_bitstream -jobs 8
wait_on_run impl_1
puts "IMPL_STATUS [get_property STATUS [get_runs impl_1]]"
