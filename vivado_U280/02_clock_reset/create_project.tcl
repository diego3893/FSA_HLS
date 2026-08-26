set stage_dir [file normalize [file dirname [info script]]]
set package_dir [file normalize [file join $stage_dir ..]]
source [file join $package_dir config project_config.tcl]

set project_name "fsa_u280_02_clock_reset"
set project_dir [file join $build_root 02_clock_reset]
file mkdir $build_root
file mkdir [file join $report_root 02_clock_reset]
create_project -force $project_name $project_dir -part $target_part
set_property target_language Verilog [current_project]
if {[llength [get_board_parts -quiet $board_part]] == 1} {
    set_property board_part $board_part [current_project]
}

add_files -norecurse [list \
    [file join $common_rtl_dir u280_pcie_reset.sv] \
    [file join $common_rtl_dir u280_clock_reset_top.sv]]
add_files -fileset constrs_1 -norecurse [file join $common_xdc_dir u280_clock_reset.xdc]

create_ip -vlnv xilinx.com:ip:clk_wiz:6.0 -module_name u280_clk_wiz_0
set_property -dict [list \
    CONFIG.PRIM_SOURCE {No_buffer} \
    CONFIG.PRIM_IN_FREQ $sys_clk_mhz \
    CONFIG.NUM_OUT_CLKS {2} \
    CONFIG.CLKOUT1_REQUESTED_OUT_FREQ $fsa_clk_mhz \
    CONFIG.CLKOUT2_REQUESTED_OUT_FREQ $hbm_axi_clk_mhz \
    CONFIG.USE_LOCKED {true} \
    CONFIG.USE_RESET {true} \
    CONFIG.RESET_TYPE {ACTIVE_HIGH}] [get_ips u280_clk_wiz_0]

set_property top u280_clock_reset_top [get_filesets sources_1]
update_compile_order -fileset sources_1
generate_target all [get_ips u280_clk_wiz_0]
report_ip_status -file [file join $report_root 02_clock_reset ip_status.rpt]
puts "PROJECT_CREATED=[file join $project_dir ${project_name}.xpr]"
close_project

