# 本脚本只进行联合C仿真。
# InputDelayer、SystolicArray和OutputDelayer是三个独立顶层，单个HLS
# solution不能把它们同时替换成一份联合RTL。实现系统级组合顶层后，再打开cosim。

set SCRIPT_DIR   [file dirname [file normalize [info script]]]
set PROJECT_ROOT [file normalize [file join $SCRIPT_DIR "../.."]]
set HLS_PROJECT_DIR [file join $SCRIPT_DIR "build"]

open_project -reset $HLS_PROJECT_DIR

# csim需要指定一个top；testbench本身还会调用input_delayer_top。
set_top systolic_array_top

set CFLAGS "-std=c++14 -I[file join $PROJECT_ROOT include]"

add_files [file join $PROJECT_ROOT "src/hls/input_delayer_top.cpp"]   -cflags $CFLAGS
add_files [file join $PROJECT_ROOT "src/hls/systolic_array_top.cpp"] -cflags $CFLAGS
add_files [file join $PROJECT_ROOT "src/hls/output_delayer_top.cpp"]  -cflags $CFLAGS
add_files [file join $PROJECT_ROOT "src/core/delayer.cpp"]           -cflags $CFLAGS
add_files [file join $PROJECT_ROOT "src/core/systolic_array.cpp"]    -cflags $CFLAGS
add_files [file join $PROJECT_ROOT "src/core/pe.cpp"]                -cflags $CFLAGS
add_files [file join $PROJECT_ROOT "src/core/cmp.cpp"]               -cflags $CFLAGS
add_files [file join $PROJECT_ROOT "src/core/arithmetic.cpp"]        -cflags $CFLAGS

add_files -tb [file join $PROJECT_ROOT "tests/hls/test_delayer_sa.cpp"] \
    -cflags $CFLAGS

open_solution -reset "solution1" -flow_target vivado

set_part {xcvu37p_CIV-fsvh2892-2-e}
create_clock -period 10 -name default

csim_design

exit
