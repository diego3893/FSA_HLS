# Run only after program_and_inventory.tcl has programmed the reviewed bit/ltx.
# Uses volatile VIO controls; never writes flash or configures another target.
# Vivado 2024.2 UG835: commit/refresh_hw_vio, run/wait_on_hw_ila,
# upload_hw_ila_data, write_hw_ila_data.
if {[llength $argv]!=2} { error "Expected matching probes and NEW evidence directory" }
set probes_file [file normalize [lindex $argv 0]]
set evidence_dir [file normalize [lindex $argv 1]]
if {![file isfile $probes_file] || [file exists $evidence_dir]} {
    error "Missing probes or evidence directory already exists"
}
file mkdir $evidence_dir

proc named_probe {core net} {
    # The generated LTX names are the actual nets (e.g. flags_1), not
    # probe_in0/probe0. Restrict to the selected core and require uniqueness.
    set result [get_hw_probes -of_objects $core -filter "NAME =~ *${net}*"]
    if {[llength $result]!=1} { error "Expected one probe net=$net core=$core" }
    return [lindex $result 0]
}
proc read_hex {probe} {
    set value [string trim [get_property INPUT_VALUE $probe]]
    if {![regexp {^(0x)?[0-9a-fA-F]+$} $value]} { error "Unknown VIO input $probe=$value" }
    scan $value %x number
    return $number
}
proc snapshot {vio probes channel label} {
    refresh_hw_vio $vio
    set values {}
    foreach probe $probes { lappend values [read_hex $probe] }
    puts $channel "[clock milliseconds],$label,[join $values ,]"
    flush $channel
    puts "NM37_STATUS $label flags=[lindex $values 0] fail=[lindex $values 1] cases=[lindex $values 2] pc=[lindex $values 3] cycles=[lindex $values 4] state=[lindex $values 5] actual=[format %016llx [lindex $values 6]] init=[lindex $values 7]"
    return $values
}
proc capture {ila output_base} {
    wait_on_hw_ila -timeout 0.05 $ila
    set data [upload_hw_ila_data $ila]
    write_hw_ila_data ${output_base}.ila $data
    write_hw_ila_data -csv_file ${output_base}.csv $data
}

open_hw_manager
connect_hw_server -url localhost:3121
set target [get_hw_targets -filter {NAME =~ */210017937722A}]
if {[llength $target]!=1} { error "Expected verified NM37 JTAG target" }
current_hw_target $target
open_hw_target
set device [get_hw_devices -filter {PART =~ xcvu37p*}]
if {[llength $device]!=1 || [llength [get_hw_devices]]!=1} { error "Expected only VU37P" }
current_hw_device $device
set_property PROBES.FILE $probes_file $device
set_property FULL_PROBES.FILE $probes_file $device
refresh_hw_device $device
set vio [get_hw_vios -of_objects $device -filter {CELL_NAME =~ *u_vio}]
set ila [get_hw_ilas -of_objects $device -filter {CELL_NAME =~ *u_ila}]
if {[llength $vio]!=1 || [llength $ila]!=1} { error "Expected selftest VIO and ILA" }
# Ordered nets match split_d_nm37_top.sv and the generated LTX mapping.
set inputs {}
foreach net {flags fail_code cases_done debug_pc last_case_cycles debug_state actual_word init_done} {
    lappend inputs [named_probe $vio $net]
}
set run_probe [named_probe $vio run_test]
set stress_probe [named_probe $vio stress_enable]
set channel [open [file join $evidence_dir vio_status.csv] w]
puts $channel "host_ms,label,flags,fail_code,cases_done,pc,last_case_cycles,state,actual_word,init_done"
report_property $device -file [file join $evidence_dir device.rpt]
foreach core [list $vio $ila] { report_property $core }
set_property OUTPUT_VALUE 0 $run_probe
set_property OUTPUT_VALUE 0 $stress_probe
commit_hw_vio $vio
set deadline [expr {[clock milliseconds]+30000}]
while {1} {
    set values [snapshot $vio $inputs $channel readiness]
    if {[lindex $values 7]==1 && ([lindex $values 0]&1)==0} { break }
    if {[clock milliseconds]>$deadline} { error "HBM initialization/idle timeout" }
    after 500
}
foreach stress {0 1} {
    set label [expr {$stress ? "stress" : "normal"}]
    set_property OUTPUT_VALUE 0 $run_probe
    set_property OUTPUT_VALUE $stress $stress_probe
    commit_hw_vio $vio
    after 500
    reset_hw_ila $ila
    set_property CONTROL.DATA_DEPTH 1024 $ila
    set_property CONTROL.TRIGGER_POSITION 128 $ila
    set_property CONTROL.TRIGGER_MODE BASIC_ONLY $ila
    set_property CONTROL.TRIGGER_CONDITION AND $ila
    set_property CONTROL.CAPTURE_MODE ALWAYS $ila
    # Arm on busy=1, done/pass/fail=0: the preceding run's latched PASS
    # cannot satisfy this condition. Save evidence of actual new-run acceptance.
    set flag_probe [named_probe $ila flags]
    set_property TRIGGER_COMPARE_VALUE {eq4'b0001} $flag_probe
    run_hw_ila $ila
    set_property OUTPUT_VALUE 1 $run_probe
    commit_hw_vio $vio
    capture $ila [file join $evidence_dir ${label}_start]
    set deadline [expr {[clock milliseconds]+90000}]
    while {1} {
        set values [snapshot $vio $inputs $channel $label]
        if {([lindex $values 0]&2)!=0} { break }
        if {[clock milliseconds]>$deadline} { error "$label board-test timeout" }
        after 500
    }
    # Capture terminal status on both success and failure before checking gates.
    run_hw_ila -trigger_now $ila
    capture $ila [file join $evidence_dir ${label}_terminal]
    if {[lindex $values 0]!=6 || [lindex $values 1]!=0 || [lindex $values 2]!=26 || [lindex $values 7]!=1} {
        error "$label failed: flags/fail/cases/init=$values"
    }
    puts "NM37_BOARD_PASS mode=$label cases=26 flags=6 fail_code=0 init_done=1"
}
set_property OUTPUT_VALUE 0 $run_probe
set_property OUTPUT_VALUE 0 $stress_probe
commit_hw_vio $vio
close $channel
close_hw_target
disconnect_hw_server
close_hw_manager
puts "ALL_NM37_BOARD_SELFTEST_GATES_PASS cases=52"
