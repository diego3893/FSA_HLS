if {[llength $argv]<1 || [llength $argv]>2} { error "Expected project path and optional rtl|hbm_tlm model" }
set model rtl
if {[llength $argv]==2} { set model [lindex $argv 1] }
if {$model ni {rtl hbm_tlm}} { error "Unsupported simulation model: $model" }
set package_dir [file normalize [file join [file dirname [info script]] ..]]
open_project [file normalize [lindex $argv 0]]
# Keep FSA/controller/SmartConnect RTL. Only the official HBM model may
# switch to vendor-supported TLM for full-vector functional checking.
set hbm [get_ips -filter {NAME =~ *hbm_0_0}]
if {[llength $hbm]!=1} { error "Expected one real HBM IP" }
set selected [expr {$model eq "hbm_tlm" ? "tlm" : "rtl"}]
if {$selected ni [get_property ALLOWED_SIM_MODELS $hbm]} { error "HBM model unsupported" }
set_property SELECTED_SIM_MODEL $selected $hbm
set defines [get_property VERILOG_DEFINE [get_filesets sim_1]]
set index [lsearch -exact $defines NM37_HBM_TLM_FUNCTIONAL]
if {$index>=0} { set defines [lreplace $defines $index $index] }
if {$model eq "hbm_tlm"} { lappend defines NM37_HBM_TLM_FUNCTIONAL }
set_property VERILOG_DEFINE $defines [get_filesets sim_1]
foreach ip [get_ips] {
    if {[string match *hbm* $ip] || [string match *interconnect* $ip]} {
        puts "NM37_SIM_MODEL $ip allowed=[get_property ALLOWED_SIM_MODELS $ip] selected=[get_property SELECTED_SIM_MODEL $ip]"
    }
}
add_files -fileset sim_1 -norecurse [file join $package_dir sim tb_split_d_system.sv]
add_files -fileset sim_1 -norecurse [file join $package_dir sim axi_watch.sv]
add_files -fileset sim_1 -norecurse [file join $package_dir sim core_latency_watch.sv]
set_property top tb_split_d_system [get_filesets sim_1]
set_property xsim.simulate.runtime {all} [get_filesets sim_1]
set_property xsim.simulate.log_all_signals false [get_filesets sim_1]
update_compile_order -fileset sim_1
launch_simulation
close_sim
close_project
