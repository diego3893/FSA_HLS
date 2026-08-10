set RUN_CSIM  1
set RUN_COSIM 1
set EXPORT_IP 1

# 脚本所在目录为 hls/cmp，项目根目录位于其上两级。
set SCRIPT_DIR  [file dirname [file normalize [info script]]]
set PROJECT_ROOT [file normalize [file join $SCRIPT_DIR "../.."]]

# HLS生成的工程和报告统一保存在hls/cmp/build中。
set HLS_PROJECT_DIR [file join $SCRIPT_DIR "build"]

open_project -reset $HLS_PROJECT_DIR

# 指定需要综合成硬件IP的顶层函数。
set_top cmp_top

# 不要在-I路径外再加转义双引号，否则Vitis会把引号当成路径字符。
set CFLAGS "-std=c++14 -I[file join $PROJECT_ROOT include]"

# CMP顶层、CMP核心逻辑及其调用的算术辅助函数。
add_files [file join $PROJECT_ROOT "src/hls/cmp_top.cpp"]       -cflags $CFLAGS
add_files [file join $PROJECT_ROOT "src/core/cmp.cpp"]          -cflags $CFLAGS
add_files [file join $PROJECT_ROOT "src/core/arithmetic.cpp"]   -cflags $CFLAGS

# C仿真和RTL协同仿真使用同一个testbench。
if {$RUN_CSIM || $RUN_COSIM} {
    add_files -tb [file join $PROJECT_ROOT "tests/hls/test_cmp_top.cpp"] \
        -cflags $CFLAGS
}

open_solution -reset "solution1" -flow_target vivado

# 与PE使用相同的目标FPGA型号和10 ns时钟周期。
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
