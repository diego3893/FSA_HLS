# Reuse a routed checkpoint; preserve the boundary constraints and fix data delay.
if {[llength $argv] != 2} { error "Expected input routed DCP and new output directory" }
set checkpoint [file normalize [lindex $argv 0]]
set output_dir [file normalize [lindex $argv 1]]
if {[file exists $output_dir]} { error "Use a new output directory" }
file mkdir $output_dir
cd $output_dir
set_param general.maxThreads 8
open_checkpoint $checkpoint
report_timing -from [all_registers] -to [all_registers] -delay_type min -max_paths 10 -file internal_hold_before.rpt
set f [open hold_violators_before.tsv w]
puts $f [join {source destination slack} "\t"]
foreach path [get_timing_paths -delay_type min -slack_lesser_than 0 -max_paths 100000 -nworst 1] {
    puts $f [join [list [get_property STARTPOINT_PIN $path] [get_property ENDPOINT_PIN $path] [get_property SLACK $path]] "\t"]
}
close $f
phys_opt_design -post_route -hold_fix
route_design -directive Default
write_checkpoint routed.dcp
report_timing_summary -delay_type min_max -report_unconstrained -check_timing_verbose -max_paths 50 -file timing_summary.rpt
report_timing -delay_type min -max_paths 30 -path_type full_clock_expanded -file hold_paths.rpt
report_timing -delay_type max -max_paths 30 -path_type full_clock_expanded -file setup_paths.rpt
report_timing -from [all_registers] -to [all_registers] -delay_type min -max_paths 10 -file internal_hold_after.rpt
report_route_status -file route_status.rpt
report_utilization -hierarchical -file utilization.rpt
report_drc -file drc.rpt
write_xdc effective_constraints.xdc
puts "OOC_HOLD_REPAIR_REPORTS_COMPLETE"
