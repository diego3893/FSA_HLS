if {[llength $argv] != 1} { error "Expected project path" }
set package_dir [file normalize [file join [file dirname [info script]] ..]]
open_project [file normalize [lindex $argv 0]]
add_files -fileset sim_1 -norecurse [file join $package_dir sim tb_split_d_system.sv]
add_files -fileset sim_1 -norecurse [file join $package_dir sim axi_watch.sv]
add_files -fileset sim_1 -norecurse [file join $package_dir sim bind_axi_watch.sv]
add_files -fileset sim_1 -norecurse [file join $package_dir sim core_latency_watch.sv]
set_property top tb_split_d_system [get_filesets sim_1]
set_property xsim.simulate.runtime {all} [get_filesets sim_1]
set_property xsim.simulate.log_all_signals false [get_filesets sim_1]
update_compile_order -fileset sim_1
launch_simulation
close_sim
close_project
