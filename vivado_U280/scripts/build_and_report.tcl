if {[llength $argv] != 1} {
    error "Usage: vivado -mode batch -source scripts/build_and_report.tcl -tclargs <project.xpr>"
}

set xpr [file normalize [lindex $argv 0]]
if {![file exists $xpr]} { error "Project does not exist: $xpr" }
set package_dir [file normalize [file join [file dirname [info script]] ..]]
source [file join $package_dir config project_config.tcl]

open_project $xpr
set project_name [get_property NAME [current_project]]
set out_dir [file join $report_root $project_name]
file mkdir $out_dir

reset_run synth_1
launch_runs synth_1 -jobs 8
wait_on_run synth_1
if {![string match "*Complete*" [get_property STATUS [get_runs synth_1]]]} {
    error "Synthesis failed: [get_property STATUS [get_runs synth_1]]"
}
open_run synth_1
report_utilization -hierarchical -file [file join $out_dir post_synth_utilization.rpt]
report_timing_summary -file [file join $out_dir post_synth_timing_summary.rpt]

launch_runs impl_1 -to_step write_bitstream -jobs 8
wait_on_run impl_1
if {![string match "*Complete*" [get_property STATUS [get_runs impl_1]]]} {
    error "Implementation failed: [get_property STATUS [get_runs impl_1]]"
}
open_run impl_1
report_timing_summary -file [file join $out_dir post_route_timing_summary.rpt]
report_timing -delay_type max -max_paths 20 -file [file join $out_dir setup_paths.rpt]
report_timing -delay_type min -max_paths 20 -file [file join $out_dir hold_paths.rpt]
report_utilization -hierarchical -file [file join $out_dir post_route_utilization.rpt]
report_drc -file [file join $out_dir post_route_drc.rpt]
report_cdc -details -file [file join $out_dir post_route_cdc.rpt]
report_clock_interaction -file [file join $out_dir clock_interaction.rpt]
report_methodology -file [file join $out_dir methodology.rpt]

set setup_path [lindex [get_timing_paths -quiet -delay_type max -max_paths 1] 0]
set hold_path  [lindex [get_timing_paths -quiet -delay_type min -max_paths 1] 0]
set setup_slack [expr {$setup_path eq "" ? "NA" : [get_property SLACK $setup_path]}]
set hold_slack  [expr {$hold_path eq "" ? "NA" : [get_property SLACK $hold_path]}]
set gate_file [open [file join $out_dir timing_gate.txt] w]
puts $gate_file "SETUP_WNS=$setup_slack"
puts $gate_file "HOLD_WHS=$hold_slack"
puts $gate_file "IMPL_STATUS=[get_property STATUS [get_runs impl_1]]"
close $gate_file

set drc_errors [get_drc_violations -quiet -filter {SEVERITY == Error}]
set drc_gate_file [open [file join $out_dir drc_gate.txt] w]
puts $drc_gate_file "DRC_ERROR_COUNT=[llength $drc_errors]"
foreach violation $drc_errors {
    puts $drc_gate_file "[get_property ID $violation]: [get_property MSG $violation]"
}
close $drc_gate_file

if {[llength [get_debug_cores -quiet]] > 0} {
    write_debug_probes -force [file join $out_dir ${project_name}.ltx]
}

if {$setup_slack ne "NA" && $setup_slack < 0.0} {
    error "Setup timing failed: WNS=$setup_slack ns"
}
if {$hold_slack ne "NA" && $hold_slack < 0.0} {
    error "Hold timing failed: WHS=$hold_slack ns"
}
if {[llength $drc_errors] != 0} {
    error "Implementation has [llength $drc_errors] DRC errors; see drc_gate.txt"
}

puts "BUILD_PASS project=$project_name setup_wns=$setup_slack hold_whs=$hold_slack"
puts "REPORT_DIR=$out_dir"
close_project
