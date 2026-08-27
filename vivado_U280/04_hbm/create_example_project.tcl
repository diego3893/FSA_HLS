set stage_dir [file normalize [file dirname [info script]]]
set package_dir [file normalize [file join $stage_dir ..]]
source [file join $package_dir config project_config.tcl]
assert_required_vivado_version
set project_dir [file join $build_root 04_hbm_ip]
set example_dir [file join $build_root 04_hbm_example]
file mkdir $build_root
file mkdir [file join $report_root 04_hbm]

create_project -force fsa_u280_04_hbm_ip $project_dir -part $target_part
apply_u280_board_part_if_available
create_ip -vlnv xilinx.com:ip:hbm:1.0 -module_name hbm_0
set_property -dict [list \
    CONFIG.USER_APB_EN {false} \
    CONFIG.USER_CLK_SEL_LIST0 {AXI_00_ACLK} \
    CONFIG.USER_HBM_DENSITY {8GB} \
    CONFIG.USER_HBM_STACK {1} \
    CONFIG.USER_MC_ENABLE_APB_01 {FALSE} \
    CONFIG.USER_SWITCH_ENABLE_01 {FALSE} \
    CONFIG.USER_XSDB_INTF_EN {TRUE}] [get_ips hbm_0]
generate_target all [get_ips hbm_0]
redirect -file [file join $report_root 04_hbm hbm_properties.txt] {
    report_property -all [get_ips hbm_0]
}
open_example_project -force -dir $example_dir [get_ips hbm_0]
puts "EXAMPLE_PROJECT_CREATED=$example_dir"
