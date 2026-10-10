# Read-only timing diagnostics; no constraint relaxation or netlist edits.
if {[llength $argv] != 2} { error "Expected routed DCP and new diagnostic directory" }
set checkpoint [file normalize [lindex $argv 0]]
set output_dir [file normalize [lindex $argv 1]]
if {[file exists $output_dir]} { error "Use a new diagnostic directory" }
file mkdir $output_dir
cd $output_dir
set_param general.maxThreads 8
open_checkpoint $checkpoint
report_timing -from [all_registers] -to [all_registers] -delay_type min -max_paths 10 -file internal_hold.rpt
report_timing -from [all_registers] -to [all_registers] -delay_type max -max_paths 10 -file internal_setup.rpt
set f [open hold_violators.tsv w]
puts $f [join {source destination slack} "\t"]
foreach path [get_timing_paths -delay_type min -slack_lesser_than 0 -max_paths 100000 -nworst 1] {
    puts $f [join [list [get_property STARTPOINT_PIN $path] [get_property ENDPOINT_PIN $path] [get_property SLACK $path]] "\t"]
}
close $f
set f [open context_properties.txt w]
puts $f "clock_source=[get_property HD.CLK_SRC [get_ports ap_clk]]"
puts $f "clock_period=[get_property PERIOD [get_clocks ap_clk]]"
puts $f "black_boxes=[llength [get_cells -hier -filter {IS_BLACKBOX == 1}]]"
close $f
write_xdc diagnostic_constraints.xdc
puts "OOC_READ_ONLY_DIAGNOSTICS_COMPLETE"
close_design
