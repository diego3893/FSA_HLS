set RUN_CSIM 1
set RUN_COSIM 0
set EXPORT_IP 0

set SCRIPT_DIR [file dirname [file normalize [info script]]]
set PROJECT_ROOT [file normalize [file join $SCRIPT_DIR "../.."]]
set HLS_PROJECT_DIR [file join $SCRIPT_DIR "build"]

set PE_DIM 4
set HEAD_DIM 16
set MAX_SEQUENCE_LENGTH 4096
if {[info exists ::env(FSA_SPLIT_D_PE_DIM)]} {
    set PE_DIM $::env(FSA_SPLIT_D_PE_DIM)
}
if {[info exists ::env(FSA_SPLIT_D_HEAD_DIM)]} {
    set HEAD_DIM $::env(FSA_SPLIT_D_HEAD_DIM)
}
if {[info exists ::env(FSA_MAX_SEQUENCE_LENGTH)]} {
    set MAX_SEQUENCE_LENGTH $::env(FSA_MAX_SEQUENCE_LENGTH)
}

set QKV_DEPTH [expr {$MAX_SEQUENCE_LENGTH*$HEAD_DIM/4}]
set O_DEPTH [expr {$MAX_SEQUENCE_LENGTH*$HEAD_DIM/2}]
set CFLAGS "-std=c++14 -I[file join $PROJECT_ROOT include] -DFSA_SPLIT_D_PE_DIM=$PE_DIM -DFSA_SPLIT_D_HEAD_DIM=$HEAD_DIM -DFSA_MAX_SEQUENCE_LENGTH=$MAX_SEQUENCE_LENGTH -DFSA_SPLIT_D_DMA_AXI_QKV_DEPTH=$QKV_DEPTH -DFSA_SPLIT_D_DMA_AXI_O_DEPTH=$O_DEPTH"

puts "FSA Split-D: PE=${PE_DIM}x${PE_DIM} HEAD_DIM=$HEAD_DIM L_MAX=$MAX_SEQUENCE_LENGTH"

open_project -reset $HLS_PROJECT_DIR
set_top fsa_stream_split_d

foreach SOURCE {
    split_d/fsa_stream_split_d.cpp
    arithmetic.cpp
    fp32_raw_fma.cpp
    pe_raw_fma.cpp
    dma.cpp
    accumulator.cpp
} {
    add_files [file join $PROJECT_ROOT "src/stream/$SOURCE"] -cflags $CFLAGS
}

if {$RUN_CSIM || $RUN_COSIM} {
    add_files -tb \
        [file join $PROJECT_ROOT "tests/stream/test_fsa_stream_split_d.cpp"] \
        -cflags $CFLAGS
}

open_solution -reset "solution1" -flow_target vivado
set_part {xcvu37p_CIV-fsvh2892-2-e}
create_clock -period 10 -name default
set_clock_uncertainty 2.7

if {$RUN_CSIM} {
    csim_design
}

csynth_design

if {$RUN_COSIM} {
    cosim_design -rtl verilog
}

if {$EXPORT_IP} {
    export_design -format ip_catalog -rtl verilog
}

exit
