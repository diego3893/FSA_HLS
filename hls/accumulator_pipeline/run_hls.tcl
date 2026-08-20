set RUN_CSIM  1
set RUN_COSIM 1
set EXPORT_IP 1

set SCRIPT_DIR [file dirname [file normalize [info script]]]
set PROJECT_ROOT [file normalize [file join $SCRIPT_DIR "../.."]]
set HLS_PROJECT_DIR [file join $SCRIPT_DIR "build"]

open_project -reset $HLS_PROJECT_DIR

# 批处理循环用于度量fast token的真实Final II，避免逐事务ap_ctrl_hs开销。
set_top accumulator_pipeline_batch_top

set CFLAGS "-std=c++14 -I[file join $PROJECT_ROOT include]"

add_files [file join $PROJECT_ROOT "src/hls/accumulator_pipeline_top.cpp"] \
    -cflags $CFLAGS
add_files [file join $PROJECT_ROOT "src/core/accumulator_pipeline.cpp"] \
    -cflags $CFLAGS
add_files [file join $PROJECT_ROOT "src/core/arithmetic.cpp"] \
    -cflags $CFLAGS

if {$RUN_CSIM || $RUN_COSIM} {
    add_files -tb \
        [file join $PROJECT_ROOT "tests/hls/test_accumulator_pipeline_top.cpp"] \
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
