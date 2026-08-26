set package_dir [file normalize [file join [file dirname [info script]] ..]]
source [file join $package_dir config project_config.tcl]
create_project -force status_reg_sim [file join $build_root status_reg_sim] -part $target_part
add_files -norecurse [file join $common_rtl_dir u280_status_axil.sv]
add_files -fileset sim_1 -norecurse [file join $package_dir common sim tb_u280_status_axil.sv]
set_property top tb_u280_status_axil [get_filesets sim_1]
update_compile_order -fileset sources_1
update_compile_order -fileset sim_1
launch_simulation
run all
close_sim
close_project

