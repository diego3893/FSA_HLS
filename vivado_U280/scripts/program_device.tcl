if {[llength $argv] < 1 || [llength $argv] > 2} {
    error "Usage: vivado -mode batch -source scripts/program_device.tcl -tclargs <file.bit> ?file.ltx?"
}
set bit_file [file normalize [lindex $argv 0]]
if {![file exists $bit_file]} { error "Bitstream does not exist: $bit_file" }
set ltx_file ""
if {[llength $argv] == 2} {
    set ltx_file [file normalize [lindex $argv 1]]
    if {![file exists $ltx_file]} { error "Probe file does not exist: $ltx_file" }
}

open_hw_manager
connect_hw_server
open_hw_target
set candidates [get_hw_devices -quiet -filter {PART =~ "xcu280*"}]
if {[llength $candidates] != 1} {
    error "Expected one xcu280 hardware device, got [llength $candidates]: $candidates"
}
set device [lindex $candidates 0]
set_property PROGRAM.FILE $bit_file $device
if {$ltx_file ne ""} {
    set_property PROBES.FILE $ltx_file $device
    set_property FULL_PROBES.FILE $ltx_file $device
}
program_hw_devices $device
refresh_hw_device $device
puts "PROGRAM_PASS device=$device bit=$bit_file probes=$ltx_file"
close_hw_manager

