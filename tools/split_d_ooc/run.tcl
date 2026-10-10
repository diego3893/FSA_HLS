# Vivado2024.2: run.tcl <exported impl/verilog> <new output dir> <auto|auto_context|regions>
# Regions reuse auto's synthesis checkpoint; the optional regions.tcl is
# reviewed from the actual auto placement, never invented device coordinates.
if {[llength $argv] != 3} { error "Expected RTL directory, output directory, mode" }
set rtl_dir [file normalize [lindex $argv 0]]
set output_dir [file normalize [lindex $argv 1]]
set mode [lindex $argv 2]
set script_dir [file dirname [file normalize [info script]]]
if {$mode ni {auto auto_context regions}} { error "Unknown mode: $mode" }
if {[file exists $output_dir]} { error "Use a new output directory: $output_dir" }
file mkdir $output_dir
cd $output_dir
set_param general.maxThreads 8
create_project ooc_project [file join $output_dir project] -part xcvu37p_CIV-fsvh2892-2-e
set_property target_language Verilog [current_project]
if {$mode eq "auto"} {
    set sources [glob -nocomplain [file join $rtl_dir *.v]]
    if {[llength $sources] == 0} { error "No formally exported Verilog" }
    set_property include_dirs [list $rtl_dir] [get_filesets sources_1]
    read_verilog $sources
    # HLS-generated scripts reproduce vendor floating point configurations.
    foreach ip_script [lsort [glob -nocomplain [file join $rtl_dir *_ip.tcl]]] {
        source $ip_script
    }
    read_xdc [file join $script_dir constraints.xdc]
    synth_design -top fsa_stream_split_d -mode out_of_context -flatten_hierarchy none
    if {[llength [get_cells -hier -filter {IS_BLACKBOX == 1}]] != 0} {
        error "Unresolved IP black boxes"
    }
    write_checkpoint synthesized.dcp
    report_utilization -hierarchical -file synthesis_utilization.rpt
} else {
    set auto_dir [file join [file dirname $output_dir] auto]
    open_checkpoint [file join $auto_dir synthesized.dcp]
}
source [file join $script_dir clock_context.tcl]
opt_design
if {$mode eq "regions"} { source [file join $script_dir regions.tcl] }
write_checkpoint optimized.dcp
place_design -directive Default
phys_opt_design -directive Default
route_design -directive Default
write_checkpoint routed.dcp
report_route_status -file route_status.rpt
report_timing_summary -delay_type min_max -report_unconstrained -check_timing_verbose -max_paths 50 -file timing_summary.rpt
report_timing -delay_type max -max_paths 30 -path_type full_clock_expanded -file setup_paths.rpt
report_timing -delay_type min -max_paths 30 -path_type full_clock_expanded -file hold_paths.rpt
report_utilization -hierarchical -file utilization.rpt
report_drc -file drc.rpt
report_methodology -file methodology.rpt
report_exceptions -file exceptions.rpt
report_high_fanout_nets -timing -max_nets 30 -file fanout.rpt
report_design_analysis -congestion -file congestion.rpt
write_xdc effective_constraints.xdc
set f [open primitive_locations.tsv w]
puts $f "cell\tref\tsite\tslr"
foreach cell [get_cells -hier -filter {IS_PRIMITIVE == 1}] {
    set sites [get_sites -quiet -of_objects $cell]
    puts $f "[get_property NAME $cell]\t[get_property REF_NAME $cell]\t$sites\t[get_slrs -quiet -of_objects $sites]"
}
close $f
set f [open physical_metrics.txt w]
puts $f "Vivado=[version -short]"
puts $f "mode=$mode"
puts $f "setup_slack=[get_property SLACK [get_timing_paths -delay_type max -max_paths 1]]"
puts $f "hold_slack=[get_property SLACK [get_timing_paths -delay_type min -max_paths 1]]"
puts $f "clock_period=[get_property PERIOD [get_clocks ap_clk]]"
close $f
puts "OOC_ROUTE_AND_REPORTS_COMPLETE $mode"
close_project
