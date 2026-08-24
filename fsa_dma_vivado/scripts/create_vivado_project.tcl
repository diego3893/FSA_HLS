# Vivado 2024.2 project creation for the fsa_dma_top NM37 board test.
# Run from the fsa_dma_vivado package root:
# vivado -mode batch -source scripts/create_vivado_project.tcl \
#   -tclargs config/project_config.tcl

set script_dir  [file normalize [file dirname [info script]]]
set package_dir [file normalize [file join $script_dir ..]]

if {[llength $argv] >= 1} {
    set config_file [file normalize [lindex $argv 0]]
} else {
    set config_file [file join $package_dir config project_config.tcl]
}

if {![file exists $config_file]} {
    error "Configuration file not found: $config_file"
}
source $config_file

set required_variables {
    project_name target_part hls_ip_vlnv hls_instance_name
    synthesis_top simulation_top simulation_runtime
    clock_wizard_name vio_instance_name ila_instance_name
    input_clock_mhz output_clock_mhz ila_depth
    vio_probe_in_widths vio_probe_out_widths ila_probe_widths
}
foreach variable_name $required_variables {
    if {![info exists $variable_name]} {
        error "Missing variable in project_config.tcl: $variable_name"
    }
}

set project_dir    [file join $package_dir vivado_project]
set ip_repo_dir    [file join $package_dir ip_repo]
set rtl_dir        [file join $package_dir rtl]
set sim_dir        [file join $package_dir sim]
set constraint_dir [file join $package_dir constraints]

foreach required_dir [list $ip_repo_dir $rtl_dir $sim_dir $constraint_dir] {
    if {![file isdirectory $required_dir]} {
        error "Required directory not found: $required_dir"
    }
}

set component_files [concat \
    [glob -nocomplain -types f [file join $ip_repo_dir component.xml]] \
    [glob -nocomplain -types f [file join $ip_repo_dir * component.xml]]]
if {[llength $component_files] == 0} {
    error "No component.xml found below $ip_repo_dir. Read ip_repo/README.md and extract the complete fsa_dma_top export first."
}

create_project -force $project_name $project_dir -part $target_part
set_property target_language Verilog [current_project]
set_property simulator_language Mixed [current_project]

set_property ip_repo_paths [list $ip_repo_dir] [current_project]
update_ip_catalog
if {[llength [get_ipdefs -all $hls_ip_vlnv]] == 0} {
    error "HLS IP not found or VLNV mismatch: $hls_ip_vlnv"
}
create_ip -vlnv $hls_ip_vlnv -module_name $hls_instance_name

create_ip -vlnv xilinx.com:ip:clk_wiz:6.0 -module_name $clock_wizard_name
set_property -dict [list \
    CONFIG.PRIM_SOURCE {Differential_clock_capable_pin} \
    CONFIG.PRIM_IN_FREQ $input_clock_mhz \
    CONFIG.NUM_OUT_CLKS {1} \
    CONFIG.CLKOUT1_REQUESTED_OUT_FREQ $output_clock_mhz \
    CONFIG.USE_LOCKED {true} \
    CONFIG.USE_RESET {true} \
    CONFIG.RESET_TYPE {ACTIVE_HIGH}] [get_ips $clock_wizard_name]

create_ip -vlnv xilinx.com:ip:vio:3.0 -module_name $vio_instance_name
set vio_properties [list \
    CONFIG.C_NUM_PROBE_IN [llength $vio_probe_in_widths] \
    CONFIG.C_NUM_PROBE_OUT [llength $vio_probe_out_widths]]
set probe_index 0
foreach probe_width $vio_probe_in_widths {
    lappend vio_properties CONFIG.C_PROBE_IN${probe_index}_WIDTH $probe_width
    incr probe_index
}
set probe_index 0
foreach probe_width $vio_probe_out_widths {
    lappend vio_properties CONFIG.C_PROBE_OUT${probe_index}_WIDTH $probe_width
    lappend vio_properties CONFIG.C_PROBE_OUT${probe_index}_INIT_VAL {0x0}
    incr probe_index
}
set_property -dict $vio_properties [get_ips $vio_instance_name]

create_ip -vlnv xilinx.com:ip:ila:6.2 -module_name $ila_instance_name
set ila_properties [list \
    CONFIG.C_NUM_OF_PROBES [llength $ila_probe_widths] \
    CONFIG.C_DATA_DEPTH $ila_depth]
set probe_index 0
foreach probe_width $ila_probe_widths {
    lappend ila_properties CONFIG.C_PROBE${probe_index}_WIDTH $probe_width
    incr probe_index
}
set_property -dict $ila_properties [get_ips $ila_instance_name]

set rtl_files [concat \
    [glob -nocomplain -types f [file join $rtl_dir *.v]] \
    [glob -nocomplain -types f [file join $rtl_dir *.sv]]]
set sim_files [concat \
    [glob -nocomplain -types f [file join $sim_dir *.v]] \
    [glob -nocomplain -types f [file join $sim_dir *.sv]]]
set xdc_files [glob -nocomplain -types f [file join $constraint_dir *.xdc]]

if {[llength $rtl_files] == 0} { error "No RTL files in $rtl_dir" }
if {[llength $sim_files] == 0} { error "No simulation files in $sim_dir" }
if {[llength $xdc_files] == 0} { error "No XDC files in $constraint_dir" }

add_files -norecurse $rtl_files
add_files -fileset sim_1 -norecurse $sim_files
add_files -fileset constrs_1 -norecurse $xdc_files

set_property top $synthesis_top [get_filesets sources_1]
set_property top $simulation_top [get_filesets sim_1]
set_property xsim.simulate.runtime $simulation_runtime [get_filesets sim_1]

update_compile_order -fileset sources_1
update_compile_order -fileset sim_1
generate_target all [get_ips]
export_ip_user_files -of_objects [get_ips] -no_script -sync -force -quiet

puts ""
puts "Vivado project created successfully."
puts "Project: [file join $project_dir ${project_name}.xpr]"
puts "Next: open the project and run Behavioral Simulation manually."
close_project

