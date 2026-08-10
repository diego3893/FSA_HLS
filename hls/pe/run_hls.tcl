# =============================================================================
# 单个 PE 的 Vitis HLS 运行脚本
#
# 推荐在 FSA-HLS 根目录执行：
#   vitis_hls -f hls/pe/run_hls.tcl
#
# 当前脚本默认执行 C 综合并导出 IP。
# test_pe_top.cpp 完成后，把 RUN_CSIM 和 RUN_COSIM 改成 1 即可执行仿真。
# =============================================================================

set RUN_CSIM  0
set RUN_COSIM 0
set EXPORT_IP 1

set SCRIPT_DIR  [file dirname [file normalize [info script]]]
set PROJECT_ROOT [file normalize [file join $SCRIPT_DIR "../.."]]

set HLS_PROJECT_DIR [file join $SCRIPT_DIR "build"]

open_project -reset $HLS_PROJECT_DIR

set_top pe_top

set CFLAGS "-std=c++14 -I\"[file join $PROJECT_ROOT include]\""

add_files [file join $PROJECT_ROOT "src/hls/pe_top.cpp"]       -cflags $CFLAGS
add_files [file join $PROJECT_ROOT "src/core/pe.cpp"]          -cflags $CFLAGS
add_files [file join $PROJECT_ROOT "src/core/arithmetic.cpp"]  -cflags $CFLAGS

if {$RUN_CSIM || $RUN_COSIM} {
    add_files -tb [file join $PROJECT_ROOT "tests/hls/test_pe_top.cpp"] \
        -cflags $CFLAGS
}

open_solution -reset "solution1" -flow_target vivado

# 修改FPGA型号
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
