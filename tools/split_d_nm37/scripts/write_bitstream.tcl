# Run only after the system simulation and routed CDC/bus-skew review pass.
if {[llength $argv]!=2} { error "Expected routed checkpoint and new output directory" }
source [file join [file dirname [info script]] check_clocks.tcl]
set checkpoint [file normalize [lindex $argv 0]]
set output_dir [file normalize [lindex $argv 1]]
if {[file exists $output_dir]} { error "Output exists: $output_dir" }
open_checkpoint $checkpoint
check_nm37_clocks
if {[get_property PART [current_design]] ne "xcvu37p_CIV-fsvh2892-2-e"} {
    error "Unexpected board device"
}
set setup [get_property SLACK [get_timing_paths -delay_type max -max_paths 1]]
set hold [get_property SLACK [get_timing_paths -delay_type min -max_paths 1]]
if {$setup<0 || $hold<0} { error "Setup/hold gate failed" }
set core_clock [get_clocks -of_objects [get_pins -hier -filter {NAME =~ */fsa_0/ap_clk}]]
if {[llength $core_clock]!=1 || abs([get_property PERIOD $core_clock]-10.0)>0.001} {
    error "Core clock must remain 100MHz"
}
file mkdir $output_dir
foreach command {report_drc report_methodology} {
    set report [file join $output_dir ${command}.rpt]
    $command -file $report
    set f [open $report r]
    set contents [read $f]
    close $f
    if {[regexp {Critical Warning|\|\s*Error\s*\|} $contents]} {
        error "Critical/error gate failed: $command"
    }
}
report_clocks -file [file join $output_dir clocks.rpt]
report_bus_skew -warn_on_violation -file [file join $output_dir bus_skew.rpt]
write_debug_probes [file join $output_dir split_d_nm37.ltx]
write_bitstream [file join $output_dir split_d_nm37.bit]
puts "NM37_BITSTREAM_COMPLETE setup=$setup hold=$hold"
close_design
