set RUN_CSIM  1
set RUN_COSIM 1
set EXPORT_IP 1

set stage_dir [file normalize [file dirname [info script]]]
set package_dir [file normalize [file join $stage_dir ..]]
set project_root [file normalize [file join $package_dir ..]]
source [file join $package_dir config project_config.tcl]

# Vitis HLS 2020.2 normally provides the Vivado-style version command. Keep
# the guard conditional so older 2020.2 launcher builds without it still run;
# the shell-level `vitis_hls -version` check remains mandatory in the guide.
if {[llength [info commands version]] != 0 && ![catch {version -short} hls_version]} {
    puts "VITIS_HLS_VERSION=$hls_version"
    if {![string match "${required_vitis_hls_version}*" $hls_version]} {
        error "This package requires Vitis HLS $required_vitis_hls_version; found $hls_version"
    }
} else {
    puts "WARNING: this Vitis HLS Tcl shell cannot report its version; verify vitis_hls -version is 2020.2 before continuing."
}

set hls_project_dir [file join $package_dir build hls_fsa_dma_u280]
set export_dir [file join $package_dir ip_export]
file mkdir $export_dir

open_project -reset $hls_project_dir
set_top fsa_dma_top

set SA_ROWS 4
set SA_COLS 4
set MAX_SEQUENCE_LENGTH 4096
set QKV_DEPTH [expr {$MAX_SEQUENCE_LENGTH*$SA_ROWS/4}]
set O_DEPTH [expr {$MAX_SEQUENCE_LENGTH*$SA_ROWS/2}]
set CFLAGS "-std=c++14 -I[file join $project_root include] -DFSA_SA_ROWS=$SA_ROWS -DFSA_SA_COLS=$SA_COLS -DFSA_MAX_SEQUENCE_LENGTH=$MAX_SEQUENCE_LENGTH -DFSA_DMA_AXI_QKV_DEPTH=$QKV_DEPTH -DFSA_DMA_AXI_O_DEPTH=$O_DEPTH"

set design_sources {
    src/hls/fsa_dma_top.cpp
    src/hls/fsa_core_request_top.cpp
    src/core/dma.cpp
    src/core/fsa_core_datapath.cpp
    src/core/execution_plan.cpp
    src/core/banked_sram.cpp
    src/core/delayer.cpp
    src/core/systolic_array.cpp
    src/core/pe.cpp
    src/core/cmp.cpp
    src/core/accumulator.cpp
    src/core/arithmetic.cpp
}
foreach relative_source $design_sources {
    set source_file [file join $project_root $relative_source]
    if {![file exists $source_file]} { error "Missing HLS source: $source_file" }
    add_files $source_file -cflags $CFLAGS
}
set testbench [file join $project_root tests hls test_fsa_dma_top.cpp]
if {![file exists $testbench]} { error "Missing HLS testbench: $testbench" }
add_files -tb $testbench -cflags $CFLAGS

open_solution -reset solution1 -flow_target vivado
set_part $target_part
create_clock -period 10 -name default

if {$RUN_CSIM} { csim_design }
csynth_design
if {$RUN_COSIM} { cosim_design -rtl verilog }
if {$EXPORT_IP} {
    export_design -format ip_catalog -rtl verilog \
        -output [file join $export_dir fsa_dma_top_u280.zip]
}
exit
