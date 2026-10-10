# Volatile JTAG configuration only. No flash/configuration-memory operations.
# Run after system simulation, routed timing/CDC/bus-skew and bitstream gates.
if {[llength $argv]!=2} { error "Expected reviewed bitstream and matching probes" }
set bit_file [file normalize [lindex $argv 0]]
set ltx_file [file normalize [lindex $argv 1]]
foreach path [list $bit_file $ltx_file] {
    if {![file isfile $path]} { error "Missing board artifact: $path" }
}
open_hw_manager
connect_hw_server -url localhost:3121
set target [get_hw_targets -filter {NAME =~ */210017937722A}]
if {[llength $target]!=1} { error "Expected the verified NM37 JTAG target" }
current_hw_target $target
open_hw_target
set device [get_hw_devices -filter {PART =~ xcvu37p*}]
if {[llength $device]!=1 || [llength [get_hw_devices]]!=1} {
    error "Expected only the verified VU37P device on this target"
}
current_hw_device $device
report_property $device
set_property PROGRAM.FILE $bit_file $device
set_property PROBES.FILE $ltx_file $device
set_property FULL_PROBES.FILE $ltx_file $device
program_hw_devices $device
refresh_hw_device $device
puts "NM37_PROGRAMMED target=$target device=$device bit=$bit_file probes=$ltx_file"
foreach vio [get_hw_vios -of_objects $device] {
    refresh_hw_vio $vio
    report_property $vio
    foreach probe [get_hw_probes -of_objects $vio] { report_property $probe }
}
close_hw_target
disconnect_hw_server
close_hw_manager
