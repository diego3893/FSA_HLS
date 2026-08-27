set stage_dir [file normalize [file dirname [info script]]]
set package_dir [file normalize [file join $stage_dir ..]]
source [file join $package_dir config project_config.tcl]
file mkdir $report_root

set report_file [open [file join $report_root 00_preflight.txt] w]
proc record {channel key value} {
    puts "$key=$value"
    puts $channel "$key=$value"
}

set vivado_version [version -short]
set vivado_executable [file normalize [info nameofexecutable]]
if {[info exists ::env(XILINX_VIVADO)]} {
    set vivado_root $::env(XILINX_VIVADO)
} else {
    set vivado_root "<unset>"
}
set target_matches [get_parts -quiet $target_part]
set u280_candidates [get_parts -quiet xcu280*]
set board_candidates [get_board_parts -quiet $board_part_pattern]
set selected_board_part [resolve_u280_board_part]

record $report_file VIVADO_VERSION $vivado_version
record $report_file REQUIRED_VIVADO_VERSION $required_vivado_version
record $report_file VIVADO_EXECUTABLE $vivado_executable
record $report_file XILINX_VIVADO $vivado_root
record $report_file TARGET_PART $target_part
record $report_file TARGET_PART_COUNT [llength $target_matches]
record $report_file U280_PART_CANDIDATES [expr {
    [llength $u280_candidates] == 0 ? "<none>" : [join $u280_candidates ","]
}]
record $report_file PREFERRED_BOARD_PART $preferred_board_part
record $report_file BOARD_PART_CANDIDATES [expr {
    [llength $board_candidates] == 0 ? "<none>" : [join $board_candidates ","]
}]
record $report_file BOARD_PART_SELECTED [expr {
    $selected_board_part eq "" ? "<none>" : $selected_board_part
}]

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

if {![string match "${required_vivado_version}*" $vivado_version]} {
    record $report_file IP_CATALOG_CHECK SKIPPED_WRONG_VIVADO_VERSION
    close $report_file
    error "This package requires Vivado $required_vivado_version; found $vivado_version"
}

if {[llength $target_matches] != 1} {
    set candidate_text [expr {
        [llength $u280_candidates] == 0 ? "<none>" : [join $u280_candidates ","]
    }]
    record $report_file IP_CATALOG_CHECK SKIPPED_NO_TARGET_PART
    close $report_file
    error "Target part is unavailable: $target_part. U280 candidates: $candidate_text. Source the intended Vivado 2020.2 settings64.sh or install Virtex UltraScale+ HBM/U280 device support, then rerun preflight."
}

# An in-memory project initializes the part-specific IP catalog. Querying
# get_ipdefs before this point can incorrectly return zero for every IP.
create_project -in_memory -part $target_part
update_ip_catalog
record $report_file IP_CATALOG_CHECK INITIALIZED

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

if {$selected_board_part eq ""} {
    record $report_file BOARD_PART_STATUS WARNING_NOT_INSTALLED
} else {
    record $report_file BOARD_PART_STATUS AVAILABLE
}
close $report_file
close_project

if {[llength $missing] != 0} {
    error "Missing required IP definitions after IP catalog initialization: $missing"
}
puts "PREFLIGHT_PASS"
