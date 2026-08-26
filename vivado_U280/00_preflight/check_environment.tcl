set stage_dir [file normalize [file dirname [info script]]]
set package_dir [file normalize [file join $stage_dir ..]]
source [file join $package_dir config project_config.tcl]
file mkdir $report_root

set report_file [open [file join $report_root 00_preflight.txt] w]
proc record {channel key value} {
    puts "$key=$value"
    puts $channel "$key=$value"
}

record $report_file VIVADO_VERSION [version -short]
record $report_file TARGET_PART_COUNT [llength [get_parts -quiet $target_part]]
record $report_file BOARD_PART_COUNT [llength [get_board_parts -quiet $board_part]]

set required_ipdefs {
    xilinx.com:ip:xdma:4.1
    xilinx.com:ip:hbm:1.0
    xilinx.com:ip:clk_wiz:6.0
    xilinx.com:ip:proc_sys_reset:5.0
    xilinx.com:ip:axi_clock_converter:2.1
    xilinx.com:ip:smartconnect:1.0
    xilinx.com:ip:ila:6.2
    xilinx.com:ip:xlconstant:1.1
}
set missing {}
foreach ipdef $required_ipdefs {
    set count [llength [get_ipdefs -all -quiet $ipdef]]
    record $report_file "IPDEF_$ipdef" $count
    if {$count == 0} { lappend missing $ipdef }
}

set component_files [concat \
    [glob -nocomplain -types f [file join $hls_ip_repo component.xml]] \
    [glob -nocomplain -types f [file join $hls_ip_repo * component.xml]] \
    [glob -nocomplain -types f [file join $hls_ip_repo * * component.xml]]]
record $report_file U280_HLS_COMPONENT_COUNT [llength $component_files]
if {[llength $component_files] > 0} {
    record $report_file U280_HLS_COMPONENT [lindex $component_files 0]
} else {
    puts "INFO: stage 01 has not populated vivado_U280/ip_repo yet."
}
close $report_file

if {[llength [get_parts -quiet $target_part]] != 1} {
    error "Target part is unavailable: $target_part"
}
if {[llength $missing] != 0} {
    error "Missing required IP definitions: $missing"
}
if {![string match "2024.2*" [version -short]]} {
    error "This package requires Vivado 2024.2; found [version -short]"
}
puts "PREFLIGHT_PASS"
