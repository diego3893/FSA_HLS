# NM37 real-HBM integration, independent of the official HLS test flow.
# Usage: vivado -mode batch -source create_project.tcl -tclargs <formal IP dir> <new build dir>
if {[llength $argv] != 2} { error "Expected formal IP directory and new build directory" }
set ip_repo [file normalize [lindex $argv 0]]
set output_dir [file normalize [lindex $argv 1]]
set package_dir [file normalize [file join [file dirname [info script]] ..]]
if {[file exists $output_dir]} { error "Output exists: $output_dir" }
if {![file exists [file join $ip_repo component.xml]]} { error "Missing formal component.xml" }
file mkdir $output_dir
create_project split_d_nm37 [file join $output_dir project] -part xcvu37p_CIV-fsvh2892-2-e
set_property target_language Verilog [current_project]
set_property simulator_language Mixed [current_project]
set_property ip_repo_paths [list $ip_repo] [current_project]
update_ip_catalog
add_files -norecurse [glob [file join $package_dir rtl *.sv]]
# The controller uses Verilog-2001 syntax. BD module references reject a
# SystemVerilog top file; set its parser type explicitly, preserving the RTL.
set_property FILE_TYPE Verilog [get_files [file join $package_dir rtl split_d_selftest.sv]]
add_files -norecurse [file join $package_dir data split_d_program.mem]
add_files -fileset constrs_1 -norecurse [file join $package_dir constraints nm37.xdc]
set core_budget [file join $package_dir constraints core_budget.xdc]
add_files -fileset constrs_1 -norecurse $core_budget
set_property PROCESSING_ORDER LATE [get_files $core_budget]
update_compile_order -fileset sources_1
create_bd_design split_d_system

set sys_clk [create_bd_port -dir I -type clk sys_clk_100]
set_property CONFIG.FREQ_HZ 100000000 $sys_clk
set reset [create_bd_port -dir I -type rst reset_n]
set_property CONFIG.POLARITY ACTIVE_LOW $reset
set clk [create_bd_cell -type ip -vlnv xilinx.com:ip:clk_wiz:6.0 clk_wiz_0]
set_property -dict [list CONFIG.PRIM_SOURCE {No_buffer} CONFIG.PRIM_IN_FREQ {100.000} \
    CONFIG.NUM_OUT_CLKS {2} CONFIG.CLKOUT2_USED {true} CONFIG.CLKOUT1_REQUESTED_OUT_FREQ {100.000} \
    CONFIG.CLKOUT2_REQUESTED_OUT_FREQ {225.000} CONFIG.USE_LOCKED {true} \
    CONFIG.USE_RESET {true} CONFIG.RESET_TYPE {ACTIVE_LOW}] $clk
connect_bd_net $sys_clk [get_bd_pins clk_wiz_0/clk_in1]
connect_bd_net $reset [get_bd_pins clk_wiz_0/resetn]
set zero [create_bd_cell -type ip -vlnv xilinx.com:ip:xlconstant:1.1 zero]
set_property -dict [list CONFIG.CONST_WIDTH {1} CONFIG.CONST_VAL {0}] $zero
foreach domain {100 225} {
    set rst [create_bd_cell -type ip -vlnv xilinx.com:ip:proc_sys_reset:5.0 rst_$domain]
    set clock_pin [expr {$domain==100 ? "clk_out1" : "clk_out2"}]
    connect_bd_net [get_bd_pins clk_wiz_0/$clock_pin] [get_bd_pins rst_$domain/slowest_sync_clk]
    connect_bd_net $reset [get_bd_pins rst_$domain/ext_reset_in]
    connect_bd_net [get_bd_pins clk_wiz_0/locked] [get_bd_pins rst_$domain/dcm_locked]
    connect_bd_net [get_bd_pins zero/dout] [get_bd_pins rst_$domain/aux_reset_in] [get_bd_pins rst_$domain/mb_debug_sys_rst]
}

# Reproduce the existing NM37 HBM_test.bd: two stacks, 8GB total, internal APB,
# only SAXI_00 enabled, 225MHz AXI; the selftest uses its first 256MB.
set hbm [create_bd_cell -type ip -vlnv xilinx.com:ip:hbm:1.0 hbm_0]
set hbm_props [list CONFIG.USER_APB_EN {false} CONFIG.USER_AXI_CLK_FREQ {225} \
    CONFIG.USER_HBM_DENSITY {8GB} CONFIG.USER_DEBUG_EN {TRUE} CONFIG.USER_XSDB_INTF_EN {TRUE}]
for {set i 1} {$i<32} {incr i} { lappend hbm_props CONFIG.USER_SAXI_[format %02d $i] {false} }
set_property -dict $hbm_props $hbm
foreach stack {0 1} {
    # Match the implemented NM37 example: HBM's internal reference BUFG
    # receives raw IBUFDS output, avoiding cascaded global clock buffers.
    connect_bd_net $sys_clk [get_bd_pins hbm_0/HBM_REF_CLK_$stack]
    connect_bd_net [get_bd_pins clk_wiz_0/clk_out1] [get_bd_pins hbm_0/APB_${stack}_PCLK]
    connect_bd_net [get_bd_pins rst_100/peripheral_aresetn] [get_bd_pins hbm_0/APB_${stack}_PRESET_N]
}
connect_bd_net [get_bd_pins clk_wiz_0/clk_out2] [get_bd_pins hbm_0/AXI_00_ACLK]
connect_bd_net [get_bd_pins rst_225/peripheral_aresetn] [get_bd_pins hbm_0/AXI_00_ARESET_N]

set fsa [create_bd_cell -type ip -vlnv xilinx.com:hls:fsa_stream_split_d:1.0 fsa_0]
set tester [create_bd_cell -type module -reference split_d_selftest selftest_0]
connect_bd_net [get_bd_pins clk_wiz_0/clk_out1] [get_bd_pins fsa_0/ap_clk] [get_bd_pins selftest_0/clk]
connect_bd_net [get_bd_pins rst_100/peripheral_aresetn] [get_bd_pins fsa_0/ap_rst_n] [get_bd_pins selftest_0/reset_n]
connect_bd_intf_net [get_bd_intf_pins selftest_0/M_AXIL] [get_bd_intf_pins fsa_0/s_axi_control]
set ready [create_bd_cell -type ip -vlnv xilinx.com:ip:util_vector_logic:2.0 memory_ready]
set_property -dict [list CONFIG.C_OPERATION {and} CONFIG.C_SIZE {1}] $ready
connect_bd_net [get_bd_pins hbm_0/apb_complete_0] [get_bd_pins memory_ready/Op1]
connect_bd_net [get_bd_pins hbm_0/apb_complete_1] [get_bd_pins memory_ready/Op2]
connect_bd_net [get_bd_pins memory_ready/Res] [get_bd_pins selftest_0/memory_ready]

# All four original FSA AXI bundles remain distinct at the core boundary.
# SmartConnect supplies real width/protocol/clock conversion to the HBM port.
set sc [create_bd_cell -type ip -vlnv xilinx.com:ip:smartconnect:1.0 memory_interconnect]
set_property -dict [list CONFIG.NUM_SI {5} CONFIG.NUM_MI {1} CONFIG.NUM_CLKS {2}] $sc
connect_bd_net [get_bd_pins clk_wiz_0/clk_out1] [get_bd_pins memory_interconnect/aclk]
connect_bd_net [get_bd_pins clk_wiz_0/clk_out2] [get_bd_pins memory_interconnect/aclk1]
connect_bd_net [get_bd_pins rst_100/interconnect_aresetn] [get_bd_pins memory_interconnect/aresetn]
set slot 0
foreach bundle {q k v o} {
    connect_bd_intf_net [get_bd_intf_pins fsa_0/m_axi_${bundle}_gmem] [get_bd_intf_pins memory_interconnect/S[format %02d $slot]_AXI]
    incr slot
}
connect_bd_intf_net [get_bd_intf_pins selftest_0/M_AXI] [get_bd_intf_pins memory_interconnect/S04_AXI]
set hbm_intf [get_bd_intf_pins -quiet hbm_0/SAXI_00]
if {[llength $hbm_intf]!=1} { set hbm_intf [get_bd_intf_pins hbm_0/AXI_00] }
connect_bd_intf_net [get_bd_intf_pins memory_interconnect/M00_AXI] $hbm_intf
assign_bd_address

# Scalar debug connections go to top-level VIO/ILA; only board clock/reset
# are physical ports. Exposing them from BD does not add FPGA package I/O.
foreach name {run_test stress_enable} {
    set p [create_bd_port -dir I $name]
    connect_bd_net $p [get_bd_pins selftest_0/$name]
}
foreach {name width} {test_busy 1 test_done 1 test_pass 1 test_fail 1 fail_code 8 cases_done 6 last_case_cycles 32 debug_pc 13 debug_state 4 actual_word 64} {
    set pin [get_bd_pins selftest_0/$name]
    if {$width==1} { set p [create_bd_port -dir O $name] } else {
        set p [create_bd_port -dir O -from [expr {$width-1}] -to 0 $name]
    }
    connect_bd_net $pin $p
}
foreach {name pin} {ctrl_clk clk_wiz_0/clk_out1 clock_locked clk_wiz_0/locked init_done memory_ready/Res} {
    if {$name eq "ctrl_clk"} { set p [create_bd_port -dir O -type clk $name] } else {
        set p [create_bd_port -dir O $name]
    }
    connect_bd_net $p [get_bd_pins $pin]
}
validate_bd_design
foreach domain {100 225} {
    if {[get_property CONFIG.C_EXT_RESET_HIGH [get_bd_cells rst_$domain]]!=0} {
        error "Reset polarity did not propagate ACTIVE_LOW in domain $domain"
    }
}
save_bd_design
set bd [get_files split_d_system.bd]
set wrapper [make_wrapper -files $bd -top]
add_files -norecurse $wrapper
generate_target all $bd

create_ip -vlnv xilinx.com:ip:vio:3.0 -module_name split_d_vio
set_property -dict [list CONFIG.C_NUM_PROBE_IN {8} CONFIG.C_NUM_PROBE_OUT {2} \
    CONFIG.C_PROBE_IN0_WIDTH {4} CONFIG.C_PROBE_IN1_WIDTH {8} CONFIG.C_PROBE_IN2_WIDTH {6} \
    CONFIG.C_PROBE_IN3_WIDTH {13} CONFIG.C_PROBE_IN4_WIDTH {32} CONFIG.C_PROBE_IN5_WIDTH {4} \
    CONFIG.C_PROBE_IN6_WIDTH {64} CONFIG.C_PROBE_IN7_WIDTH {1} \
    CONFIG.C_PROBE_OUT0_WIDTH {1} CONFIG.C_PROBE_OUT1_WIDTH {1}] [get_ips split_d_vio]
create_ip -vlnv xilinx.com:ip:ila:6.2 -module_name split_d_ila
set_property -dict [list CONFIG.C_NUM_OF_PROBES {8} CONFIG.C_DATA_DEPTH {1024} \
    CONFIG.C_PROBE0_WIDTH {4} CONFIG.C_PROBE1_WIDTH {8} CONFIG.C_PROBE2_WIDTH {6} \
    CONFIG.C_PROBE3_WIDTH {13} CONFIG.C_PROBE4_WIDTH {32} CONFIG.C_PROBE5_WIDTH {4} \
    CONFIG.C_PROBE6_WIDTH {64} CONFIG.C_PROBE7_WIDTH {1}] [get_ips split_d_ila]
generate_target all [get_ips {split_d_vio split_d_ila}]
set_property top split_d_nm37_top [get_filesets sources_1]
set_property STEPS.SYNTH_DESIGN.ARGS.FLATTEN_HIERARCHY none [get_runs synth_1]
update_compile_order -fileset sources_1
write_bd_tcl [file join $output_dir recreate_bd.tcl]
report_ip_status -file [file join $output_dir ip_status.rpt]
redirect -file [file join $output_dir address_map.txt] { report_property [get_bd_addr_segs] }
puts "NM37_PROJECT_CREATED=[file join $output_dir project split_d_nm37.xpr]"
close_project
