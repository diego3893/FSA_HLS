# Fix only already placed BUFG LOC metadata; never reroute the baseline.
if {[llength $argv] != 2} { error "Expected routed DCP and new output directory" }
set checkpoint [file normalize [lindex $argv 0]]
set output_dir [file normalize [lindex $argv 1]]
set script_dir [file dirname [file normalize [info script]]]
if {[file exists $output_dir]} { error "Use a new output directory" }
file mkdir $output_dir
cd $output_dir
set_param general.maxThreads 8
open_checkpoint $checkpoint
set before_setup [get_property SLACK [get_timing_paths -delay_type max -max_paths 1]]
set before_hold [get_property SLACK [get_timing_paths -delay_type min -max_paths 1]]
report_utilization -hierarchical -file before_utilization.rpt
source [file join $script_dir lock_buffers.tcl]
set after_setup [get_property SLACK [get_timing_paths -delay_type max -max_paths 1]]
set after_hold [get_property SLACK [get_timing_paths -delay_type min -max_paths 1]]
if {$before_setup != $after_setup || $before_hold != $after_hold} { error "LOC metadata changed timing" }
report_utilization -hierarchical -file after_utilization.rpt
report_drc -file drc.rpt
write_xdc effective_constraints.xdc
write_checkpoint routed.dcp
set f [open metadata_comparison.txt w]
puts $f "before_setup=$before_setup"
puts $f "after_setup=$after_setup"
puts $f "before_hold=$before_hold"
puts $f "after_hold=$after_hold"
close $f
puts "OOC_BUFFER_METADATA_COMPLETE"
close_design
