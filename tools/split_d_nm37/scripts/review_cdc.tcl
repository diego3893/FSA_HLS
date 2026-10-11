# Expose encrypted-IP CDC endpoints through the CDC result objects. This
# review does not waive checks, change constraints or write a checkpoint.
if {[llength $argv]!=2} { error "Expected routed checkpoint and NEW evidence directory" }
set checkpoint [file normalize [lindex $argv 0]]
set output_dir [file normalize [lindex $argv 1]]
if {[file exists $output_dir]} { error "Output exists: $output_dir" }
open_checkpoint $checkpoint
file mkdir $output_dir
report_cdc -name nm37_cdc_review -details -file [file join $output_dir cdc_details.rpt]
set channel [open [file join $output_dir cdc_objects.tsv] w]
puts $channel "name\tseverity\ttype\texception\tdepth\tstart_clock\tend_clock\tstart_pin\tend_pin\tclassification"
set groups [dict create]
set unreviewed 0
foreach violation [get_cdc_violations -name nm37_cdc_review] {
    set severity [get_property SEVERITY $violation]
    set check [get_property CHECK $violation]
    set type [get_property CDC_TYPE $violation]
    set exception [get_property EXCEPTION $violation]
    set start [get_property STARTPOINT_PIN $violation]
    set end [get_property ENDPOINT_PIN $violation]
    set classification information
    if {$severity ne "Info"} {
        set classification UNREVIEWED
        if {$severity eq "Warning" && $check eq "CDC-15"} {
            if {$type eq "Safely Timed" && $exception eq "Max Delay Datapath Only" &&
                [string match "u_bd/split_d_system_i/memory_interconnect/*" $start] &&
                [string match "u_bd/split_d_system_i/memory_interconnect/*" $end] &&
                [string first "gen_xpm_memory_fifo" $start]>=0 &&
                [string first "gen_xpm_memory_fifo" $end]>=0} {
                set classification smartconnect_xpm_fifo_timed_payload
            } elseif {$exception eq "False Path" &&
                [string match "dbg_hub/*" $start] && [string match "dbg_hub/*" $end] &&
                [string first "U_WR_FIFO" $start]>=0 && [string first "U_WR_FIFO" $end]>=0} {
                set classification xsdb_fifo_write_payload
            } elseif {$exception eq "False Path" &&
                [string match "dbg_hub/*" $start] && [string match "dbg_hub/*" $end] &&
                [string first "U_RD_FIFO" $start]>=0 && [string first "U_RD_FIFO" $end]>=0} {
                set classification xsdb_fifo_read_payload
            }
        }
        if {$classification eq "UNREVIEWED"} { incr unreviewed }
    }
    dict incr groups "$check/$severity/$classification"
    set row [list [get_property NAME $violation] $severity $type $exception \
        [get_property DEPTH $violation] [get_property STARTPOINT_CLOCK $violation] \
        [get_property ENDPOINT_CLOCK $violation] $start $end $classification]
    puts $channel [join $row \t]
}
close $channel
dict for {group count} $groups { puts "CDC_GROUP=$group COUNT=$count" }
report_bus_skew -warn_on_violation -file [file join $output_dir bus_skew.rpt]
report_clock_interaction -file [file join $output_dir clock_interaction.rpt]
report_exceptions -coverage -file [file join $output_dir exceptions_coverage.rpt]
if {$unreviewed!=0} { error "Unreviewed CDC warnings/errors: $unreviewed" }
puts "NM37_CDC_CLASSIFICATION_COMPLETE (bus-skew report still requires slack review)"
close_design
