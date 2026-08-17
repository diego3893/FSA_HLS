set RUN_CSIM  1
set RUN_COSIM 1
set EXPORT_IP 1

set SCRIPT_DIR [file dirname [file normalize [info script]]]
set PROJECT_ROOT [file normalize [file join $SCRIPT_DIR "../.."]]
set HLS_BUILD_DIR [file join $SCRIPT_DIR "build"]
set CFLAGS "-std=c++14 -I[file join $PROJECT_ROOT include]"

# BankedSRAM在FSA中有两种不同元素位宽和端口组合，必须分别综合为两个IP。
proc build_banked_sram_top {project_name top_name test_define} {
    global RUN_CSIM RUN_COSIM EXPORT_IP
    global PROJECT_ROOT HLS_BUILD_DIR CFLAGS

    set HLS_PROJECT_DIR [file join $HLS_BUILD_DIR $project_name]
    open_project -reset $HLS_PROJECT_DIR
    set_top $top_name

    add_files [file join $PROJECT_ROOT "src/hls/banked_sram_top.cpp"] \
        -cflags $CFLAGS
    add_files [file join $PROJECT_ROOT "src/core/banked_sram.cpp"] \
        -cflags $CFLAGS

    if {$RUN_CSIM || $RUN_COSIM} {
        add_files -tb \
            [file join $PROJECT_ROOT "tests/hls/test_banked_sram_top.cpp"] \
            -cflags "$CFLAGS -D$test_define"
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

    close_project
}

build_banked_sram_top \
    "sp_ram" "sp_ram_top" "FSA_TEST_SP_RAM_TOP"
build_banked_sram_top \
    "acc_ram" "acc_ram_top" "FSA_TEST_ACC_RAM_TOP"

exit
