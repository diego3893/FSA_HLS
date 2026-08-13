set SCRIPT_DIR  [file dirname [file normalize [info script]]]
set PROJECT_ROOT [file normalize [file join $SCRIPT_DIR "../.."]]
set HLS_PROJECT_DIR [file join $SCRIPT_DIR "build_csim"]

open_project -reset $HLS_PROJECT_DIR

# 本脚本保留为accumulator_step核心函数的快速C Simulation。
# Accumulator HLS顶层的CSim、综合和Co-sim请运行同目录run_hls.tcl。
set_top accumulator_step

# add_files -cflags会自行把整段CFLAGS传给编译器。这里不能把双引号
# 写进-I路径，否则Vitis 2024.2会把引号视作目录名的一部分。
set CFLAGS "-std=c++14 -I[file join $PROJECT_ROOT include]"

add_files [file join $PROJECT_ROOT "src/core/accumulator.cpp"] -cflags $CFLAGS
add_files [file join $PROJECT_ROOT "src/core/arithmetic.cpp"]  -cflags $CFLAGS
add_files -tb [file join $PROJECT_ROOT "tests/test_accumulator.cpp"] \
    -cflags $CFLAGS

open_solution -reset "solution1" -flow_target vivado
set_part {xcvu37p_CIV-fsvh2892-2-e}
create_clock -period 10 -name default

csim_design
exit
