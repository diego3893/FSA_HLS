set RUN_CSIM 1
set RUN_COSIM 0
set EXPORT_IP 0

# CSim/CSynth are the default scope; optional scope overrides must be 0/1.
foreach SWITCH {RUN_CSIM RUN_COSIM EXPORT_IP} {
    if {[info exists ::env($SWITCH)]} {
        if {$::env($SWITCH) ni {0 1}} { error "$SWITCH must be 0 or 1" }
        set $SWITCH $::env($SWITCH)
    }
}

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
# Forward only explicit finite-engine configuration, identically to DUT and TB.
foreach PARAM {FSA_SPLIT_D_QUERY_CONTEXTS FSA_SPLIT_D_PE_HOP_CYCLES FSA_SPLIT_D_QK_FEEDBACK_CYCLES FSA_SPLIT_D_ACC_HOP_CYCLES} {
    if {[info exists ::env($PARAM)]} {
        if {![string is integer -strict $::env($PARAM)] || $::env($PARAM)<1} {
            error "$PARAM must be a positive integer"
        }
        append CFLAGS " -D$PARAM=$::env($PARAM)"
        puts "FSA Split-D engine: $PARAM=$::env($PARAM)"
    }
}
foreach PARAM {FSA_SPLIT_D_DSP_NORMALIZE FSA_SPLIT_D_DSP_MAGNITUDE} {
    if {[info exists ::env($PARAM)]} {
        if {$::env($PARAM) ni {0 1}} { error "$PARAM must be 0 or 1" }
        append CFLAGS " -D$PARAM=$::env($PARAM)"
        puts "FSA Split-D PE mapping: $PARAM=$::env($PARAM)"
    }
}
set TB_CFLAGS $CFLAGS
set LONG_CSIM_TEST 0
if {[info exists ::env(FSA_SPLIT_D_LONG_CSIM_TEST)]} {
    if {$::env(FSA_SPLIT_D_LONG_CSIM_TEST) ni {0 1}} {
        error "FSA_SPLIT_D_LONG_CSIM_TEST must be 0 or 1"
    }
    if {$::env(FSA_SPLIT_D_LONG_CSIM_TEST)} {
        set LONG_CSIM_TEST 1
        if {!$RUN_CSIM || $RUN_COSIM || $PE_DIM!=16 || $HEAD_DIM!=128 || $MAX_SEQUENCE_LENGTH<1024} {
            error "Long CSim test requires CSim-only test scope and PE16/head128/L_MAX>=1024"
        }
        append TB_CFLAGS " -DFSA_SPLIT_D_LONG_CSIM_TEST=1"
    }
}

puts "FSA Split-D: PE=${PE_DIM}x${PE_DIM} HEAD_DIM=$HEAD_DIM L_MAX=$MAX_SEQUENCE_LENGTH"
puts "FSA Split-D stages: CSIM=$RUN_CSIM COSIM=$RUN_COSIM EXPORT_IP=$EXPORT_IP"

open_project -reset $HLS_PROJECT_DIR
set_top fsa_stream_split_d

foreach SOURCE {
    split_d/fsa_stream_split_d.cpp
    split_d/split_d_controller.cpp
    split_d/split_d_compute.cpp
    split_d/split_d_dma.cpp
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
        -cflags $TB_CFLAGS
}

open_solution -reset "solution1" -flow_target vivado
set_part {xcvu37p_CIV-fsvh2892-2-e}
create_clock -period 10 -name default
set_clock_uncertainty 2.7

if {$RUN_CSIM} {
    if {$LONG_CSIM_TEST} {
        # Optimize only host C simulation; synthesis flags and hardware are unchanged.
        csim_design -O
    } else {
        # Optimize the host executable only; hardware synthesis CFLAGS stay identical.
        csim_design -O
    }
}

csynth_design

if {$RUN_COSIM} {
    cosim_design -rtl verilog -trace_level all -enable_dataflow_profiling
}

if {$EXPORT_IP} {
    export_design -format ip_catalog -rtl verilog
}

exit
