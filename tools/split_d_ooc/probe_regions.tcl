# Diagnose property interactions in memory; never overwrite the tested DCP.
if {[llength $argv] != 2} { error "Expected routed DCP and new output directory" }
set checkpoint [file normalize [lindex $argv 0]]
set output_dir [file normalize [lindex $argv 1]]
if {[file exists $output_dir]} { error "Use a new output directory" }
file mkdir $output_dir
cd $output_dir
open_checkpoint $checkpoint
set f [open property_probe.txt w]
foreach pb [get_pblocks] {
    puts $f "$pb initial_soft=[get_property IS_SOFT $pb] routing=[get_property CONTAIN_ROUTING $pb]"
    set_property IS_SOFT 0 $pb
    puts $f "$pb after_soft_0=[get_property IS_SOFT $pb] routing=[get_property CONTAIN_ROUTING $pb]"
    set_property CONTAIN_ROUTING 0 $pb
    puts $f "$pb after_routing_0=[get_property IS_SOFT $pb] routing=[get_property CONTAIN_ROUTING $pb]"
    set_property IS_SOFT 0 $pb
    puts $f "$pb final_soft=[get_property IS_SOFT $pb] routing=[get_property CONTAIN_ROUTING $pb]"
}
foreach bufg [get_cells -quiet -hier -filter {REF_NAME =~ BUFG*}] {
    puts $f "buffer=[get_property NAME $bufg] site=[get_sites -of_objects $bufg] LOC=[get_property LOC $bufg]"
}
close $f
write_xdc property_probe_constraints.xdc
puts "OOC_PROPERTY_PROBE_COMPLETE"
close_design
