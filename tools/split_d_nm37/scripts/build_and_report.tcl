# Full physical integration; constraints apply to real clocks/internal AXI.
if {[llength $argv] != 1} { error "Expected project path" }
source [file join [file dirname [info script]] check_clocks.tcl]
open_project [file normalize [lindex $argv 0]]
set report_dir [file normalize [file join [get_property DIRECTORY [current_project]] .. reports]]
file mkdir $report_dir
set_param general.maxThreads 8
launch_runs synth_1 -jobs 8
wait_on_run synth_1
if {[get_property PROGRESS [get_runs synth_1]] ne "100%"} { error "Synthesis failed" }
open_run synth_1
check_nm37_clocks
set fsa_clock_pin [get_pins -hier -filter {NAME =~ */fsa_0/ap_clk}]
if {[llength $fsa_clock_pin]!=1} { error "Cannot identify unique FSA clock pin: $fsa_clock_pin" }
set core_clock [get_clocks -of_objects $fsa_clock_pin]
if {[llength $core_clock]!=1 || abs([get_property PERIOD $core_clock]-10.0)>0.001} {
    error "FSA clock must remain 100MHz: $core_clock"
}
# core_budget.xdc applies the unchanged 2.7ns setup uncertainty in both runs.
report_utilization -hierarchical -file [file join $report_dir synthesis_utilization.rpt]
close_design
launch_runs impl_1 -to_step route_design -jobs 8
wait_on_run impl_1
if {[get_property PROGRESS [get_runs impl_1]] ne "100%"} { error "Implementation failed" }
open_run impl_1
check_nm37_clocks
set core_clock [get_clocks -of_objects [get_pins -hier -filter {NAME =~ */fsa_0/ap_clk}]]
if {[llength $core_clock]!=1 || abs([get_property PERIOD $core_clock]-10.0)>0.001} { error "Implementation core clock differs" }
report_timing_summary -delay_type min_max -check_timing_verbose -report_unconstrained -max_paths 50 -file [file join $report_dir timing_summary.rpt]
report_timing -delay_type max -path_type full_clock_expanded -max_paths 30 -file [file join $report_dir setup_paths.rpt]
report_timing -delay_type min -path_type full_clock_expanded -max_paths 30 -file [file join $report_dir hold_paths.rpt]
report_route_status -file [file join $report_dir route_status.rpt]
report_utilization -hierarchical -file [file join $report_dir utilization.rpt]
report_drc -file [file join $report_dir drc.rpt]
report_methodology -file [file join $report_dir methodology.rpt]
report_clock_interaction -file [file join $report_dir clock_interaction.rpt]
report_cdc -details -file [file join $report_dir cdc.rpt]
report_bus_skew -warn_on_violation -file [file join $report_dir bus_skew.rpt]
report_clocks -file [file join $report_dir clocks.rpt]
report_exceptions -file [file join $report_dir exceptions.rpt]
write_checkpoint [file join $report_dir routed.dcp]
write_xdc [file join $report_dir effective_constraints.xdc]
set setup [get_property SLACK [get_timing_paths -delay_type max -max_paths 1]]
set hold [get_property SLACK [get_timing_paths -delay_type min -max_paths 1]]
set f [open [file join $report_dir timing_gate.txt] w]
puts $f "setup=$setup\nhold=$hold\nclock=[get_property NAME $core_clock]"
close $f
puts "NM37_IMPLEMENTATION_REPORTS_COMPLETE setup=$setup hold=$hold"
if {$setup<0 || $hold<0} { error "Full-system setup/hold gate failed" }
foreach report {drc.rpt methodology.rpt} {
    set f [open [file join $report_dir $report] r]
    set contents [read $f]
    close $f
    if {[regexp {Critical Warning|\|\s*Error\s*\|} $contents]} {
        error "Full-system critical/error gate failed: $report"
    }
}
# Bitstream is deliberately a separate gate after timing/DRC/CDC review.
close_project
