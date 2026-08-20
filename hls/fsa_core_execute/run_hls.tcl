set RUN_CSIM  1
set RUN_COSIM 1
set EXPORT_IP 1

set SCRIPT_DIR [file dirname [file normalize [info script]]]
set PROJECT_ROOT [file normalize [file join $SCRIPT_DIR "../.."]]
set HLS_PROJECT_DIR [file join $SCRIPT_DIR "build"]

open_project -reset $HLS_PROJECT_DIR
set_top fsa_core_execute_top

set CFLAGS "-std=c++14 -I[file join $PROJECT_ROOT include]"

add_files [file join $PROJECT_ROOT "src/hls/fsa_core_execute_top.cpp"] \
    -cflags $CFLAGS
add_files [file join $PROJECT_ROOT "src/core/fsa_core_datapath.cpp"] \
    -cflags $CFLAGS
add_files [file join $PROJECT_ROOT "src/core/execution_plan.cpp"] \
    -cflags $CFLAGS
add_files [file join $PROJECT_ROOT "src/core/banked_sram.cpp"] \
    -cflags $CFLAGS
add_files [file join $PROJECT_ROOT "src/core/delayer.cpp"] \
    -cflags $CFLAGS
add_files [file join $PROJECT_ROOT "src/core/systolic_array.cpp"] \
    -cflags $CFLAGS
add_files [file join $PROJECT_ROOT "src/core/pe.cpp"] \
    -cflags $CFLAGS
add_files [file join $PROJECT_ROOT "src/core/cmp.cpp"] \
    -cflags $CFLAGS
add_files [file join $PROJECT_ROOT "src/core/accumulator.cpp"] \
    -cflags $CFLAGS
add_files [file join $PROJECT_ROOT "src/core/arithmetic.cpp"] \
    -cflags $CFLAGS

if {$RUN_CSIM || $RUN_COSIM} {
    add_files -tb \
        [file join $PROJECT_ROOT "tests/hls/test_fsa_core_execute_top.cpp"] \
        -cflags $CFLAGS
}

open_solution -reset "solution1" -flow_target vivado
set_part {xcvu37p_CIV-fsvh2892-2-e}
create_clock -period 10 -name default

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
