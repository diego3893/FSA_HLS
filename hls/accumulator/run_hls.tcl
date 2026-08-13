# 初次运行默认执行C Simulation和C Synthesis。
# CSim、综合报告检查通过后，再把RUN_COSIM改成1执行C/RTL Co-simulation。
set RUN_CSIM  1
set RUN_SYNTH 1
set RUN_COSIM 0
set EXPORT_IP 0

set SCRIPT_DIR   [file dirname [file normalize [info script]]]
set PROJECT_ROOT [file normalize [file join $SCRIPT_DIR "../.."]]
set HLS_PROJECT_DIR [file join $SCRIPT_DIR "build"]

open_project -reset $HLS_PROJECT_DIR

set_top accumulator_top

set CFLAGS "-std=c++14 -I\"[file join $PROJECT_ROOT include]\""

add_files [file join $PROJECT_ROOT "src/hls/accumulator_top.cpp"]  \
    -cflags $CFLAGS
add_files [file join $PROJECT_ROOT "src/core/accumulator.cpp"]     \
    -cflags $CFLAGS
add_files [file join $PROJECT_ROOT "src/core/arithmetic.cpp"]      \
    -cflags $CFLAGS

if {$RUN_CSIM || $RUN_COSIM} {
    add_files -tb \
        [file join $PROJECT_ROOT "tests/hls/test_accumulator_top.cpp"] \
        -cflags $CFLAGS
}

open_solution -reset "solution1" -flow_target vivado

# 与当前PE工程保持相同器件。若服务器没有该器件，请替换为实际FPGA part。
set_part {xcvu37p_CIV-fsvh2892-2-e}

create_clock -period 10 -name default

if {$RUN_CSIM} {
    csim_design
}

# Co-simulation和导出IP都依赖综合结果。
if {$RUN_SYNTH || $RUN_COSIM || $EXPORT_IP} {
    csynth_design
}

if {$RUN_COSIM} {
    cosim_design -rtl verilog
}

if {$EXPORT_IP} {
    export_design -format ip_catalog -rtl verilog
}

exit
