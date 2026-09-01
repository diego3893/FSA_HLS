set RUN_CSIM  1
set RUN_COSIM 0
set EXPORT_IP 0

set SCRIPT_DIR [file dirname [file normalize [info script]]]
set PROJECT_ROOT [file normalize [file join $SCRIPT_DIR "../.."]]
set HLS_PROJECT_DIR [file join $SCRIPT_DIR "build"]

open_project -reset $HLS_PROJECT_DIR
set_top fsa_stream_request_top

set SA_ROWS 4
set SA_COLS 4
if {[info exists ::env(FSA_SA_ROWS)]} {
    set SA_ROWS $::env(FSA_SA_ROWS)
}
if {[info exists ::env(FSA_SA_COLS)]} {
    set SA_COLS $::env(FSA_SA_COLS)
}

puts "FSA stream configuration: SA_ROWS=$SA_ROWS SA_COLS=$SA_COLS"
set CFLAGS "-std=c++14 -I[file join $PROJECT_ROOT include] -DFSA_SA_ROWS=$SA_ROWS -DFSA_SA_COLS=$SA_COLS"

add_files [file join $PROJECT_ROOT "src/hls/fsa_stream_request_top.cpp"] \
    -cflags $CFLAGS
add_files [file join $PROJECT_ROOT "src/core/stream_array.cpp"] \
    -cflags $CFLAGS
add_files [file join $PROJECT_ROOT "src/core/stream_pe.cpp"] \
    -cflags $CFLAGS
add_files [file join $PROJECT_ROOT "src/core/arithmetic.cpp"] \
    -cflags $CFLAGS

if {$RUN_CSIM || $RUN_COSIM} {
    add_files -tb \
        [file join $PROJECT_ROOT "tests/hls/test_fsa_stream_request_top.cpp"] \
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
