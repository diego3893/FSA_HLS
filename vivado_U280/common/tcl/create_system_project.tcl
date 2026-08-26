# Common builder for stage 05 (XDMA+HBM) and stage 06 (XDMA+HBM+fsa_dma_top).
# The caller must define include_fsa and stage_name before sourcing this file.

if {![info exists include_fsa] || ![info exists stage_name]} {
    error "Caller must set include_fsa and stage_name"
}

set common_tcl_dir [file normalize [file dirname [info script]]]
set package_dir [file normalize [file join $common_tcl_dir ../..]]
source [file join $package_dir config project_config.tcl]

proc require_one {objects description} {
    if {[llength $objects] != 1} {
        error "Expected exactly one $description, got [llength $objects]: $objects"
    }
    return [lindex $objects 0]
}

proc first_matching_addr_seg {patterns description} {
    foreach pattern $patterns {
        set hits [get_bd_addr_segs -quiet -hier -filter "NAME =~ $pattern"]
        if {[llength $hits] == 1} {
            return [lindex $hits 0]
        }
    }
    error "Cannot find unique address segment for $description"
}

set project_name "fsa_u280_${stage_name}"
set project_dir [file join $build_root $stage_name]
file mkdir $build_root
file mkdir [file join $report_root $stage_name]

create_project -force $project_name $project_dir -part $target_part
set_property target_language Verilog [current_project]
set_property simulator_language Mixed [current_project]
if {[llength [get_board_parts -quiet $board_part]] == 1} {
    set_property board_part $board_part [current_project]
} else {
    puts "WARNING: board part $board_part is not installed; target part remains authoritative."
}

set rtl_files [list \
    [file join $common_rtl_dir u280_pcie_reset.sv] \
    [file join $common_rtl_dir u280_status_axil.sv] \
    [file join $common_rtl_dir u280_system_top.sv]]
foreach rtl_file $rtl_files {
    if {![file exists $rtl_file]} { error "Missing RTL file: $rtl_file" }
}
add_files -norecurse $rtl_files
add_files -fileset constrs_1 -norecurse [file join $common_xdc_dir u280_pins.xdc]
update_compile_order -fileset sources_1

if {$include_fsa} {
    set components [concat \
        [glob -nocomplain -types f [file join $hls_ip_repo component.xml]] \
        [glob -nocomplain -types f [file join $hls_ip_repo * component.xml]] \
        [glob -nocomplain -types f [file join $hls_ip_repo * * component.xml]]]
    if {[llength $components] == 0} {
        error "No unpacked fsa_dma_top component.xml under $hls_ip_repo. Run stage 01 first."
    }
    set_property ip_repo_paths [list [file dirname [lindex $components 0]]] [current_project]
    update_ip_catalog
    if {[llength [get_ipdefs -all -quiet $hls_ip_vlnv]] == 0} {
        error "HLS IP not registered: $hls_ip_vlnv"
    }
}

create_bd_design "u280_system_bd"

set sys_clk_port [create_bd_port -dir I -type clk sys_clk_300]
set_property CONFIG.FREQ_HZ 300000000 $sys_clk_port
set hbm_ref_port [create_bd_port -dir I -type clk hbm_ref_clk_100]
set_property CONFIG.FREQ_HZ 100000000 $hbm_ref_port
set pcie_ref_port [create_bd_port -dir I -type clk pcie_ref_clk_100]
set_property CONFIG.FREQ_HZ 100000000 $pcie_ref_port
set pcie_gt_port [create_bd_port -dir I -type clk pcie_ref_clk_gt]
set_property CONFIG.FREQ_HZ 100000000 $pcie_gt_port
set reset_port [create_bd_port -dir I -type rst pcie_sys_rst_n]
set_property CONFIG.POLARITY ACTIVE_LOW $reset_port
set fabric_reset_port [create_bd_port -dir I -type rst fabric_reset]
set_property CONFIG.POLARITY ACTIVE_HIGH $fabric_reset_port

set clk_wiz [create_bd_cell -type ip -vlnv xilinx.com:ip:clk_wiz:6.0 clk_wiz_0]
set_property -dict [list \
    CONFIG.PRIM_SOURCE {No_buffer} \
    CONFIG.PRIM_IN_FREQ $sys_clk_mhz \
    CONFIG.NUM_OUT_CLKS {2} \
    CONFIG.CLKOUT1_REQUESTED_OUT_FREQ $fsa_clk_mhz \
    CONFIG.CLKOUT2_REQUESTED_OUT_FREQ $hbm_axi_clk_mhz \
    CONFIG.USE_LOCKED {true} \
    CONFIG.USE_RESET {true} \
    CONFIG.RESET_TYPE {ACTIVE_HIGH}] $clk_wiz
connect_bd_net $sys_clk_port [get_bd_pins clk_wiz_0/clk_in1]
connect_bd_net $fabric_reset_port [get_bd_pins clk_wiz_0/reset]

set const_zero [create_bd_cell -type ip -vlnv xilinx.com:ip:xlconstant:1.1 const_zero]
set_property -dict [list CONFIG.CONST_WIDTH {1} CONFIG.CONST_VAL {0}] $const_zero

foreach domain {100 225} {
    set rst_cell [create_bd_cell -type ip -vlnv xilinx.com:ip:proc_sys_reset:5.0 rst_${domain}]
    connect_bd_net [get_bd_pins clk_wiz_0/clk_out[expr {$domain == 100 ? 1 : 2}]] \
        [get_bd_pins rst_${domain}/slowest_sync_clk]
    connect_bd_net $fabric_reset_port [get_bd_pins rst_${domain}/ext_reset_in]
    connect_bd_net [get_bd_pins clk_wiz_0/locked] [get_bd_pins rst_${domain}/dcm_locked]
    connect_bd_net [get_bd_pins const_zero/dout] \
        [get_bd_pins rst_${domain}/aux_reset_in] \
        [get_bd_pins rst_${domain}/mb_debug_sys_rst]
}

set xdma [create_bd_cell -type ip -vlnv xilinx.com:ip:xdma:4.1 xdma_0]
set_property -dict [list \
    CONFIG.functional_mode {DMA} \
    CONFIG.mode_selection {Advanced} \
    CONFIG.en_gt_selection {true} \
    CONFIG.select_quad {GTY_Quad_227} \
    CONFIG.cfg_mgmt_if {false} \
    CONFIG.pcie_extended_tag {true} \
    CONFIG.pf0_base_class_menu {Processing_accelerators} \
    CONFIG.pf0_sub_class_interface_menu {Unknown} \
    CONFIG.pf0_msi_enabled {true} \
    CONFIG.pf0_msix_enabled {false} \
    CONFIG.xdma_num_usr_irq {1} \
    CONFIG.pcie_blk_locn {PCIE4C_X1Y0} \
    CONFIG.ref_clk_freq {100_MHz} \
    CONFIG.axisten_freq {250} \
    CONFIG.axi_addr_width {64} \
    CONFIG.axi_data_width {256_bit} \
    CONFIG.axi_id_width {4} \
    CONFIG.axilite_master_en {true} \
    CONFIG.axilite_master_scale {Megabytes} \
    CONFIG.axilite_master_size {32} \
    CONFIG.pf0_bar0_scale {Kilobytes} \
    CONFIG.pf0_bar0_size {128} \
    CONFIG.pciebar2axibar_axil_master {0x00000000} \
    CONFIG.pl_link_cap_max_link_width {X8} \
    CONFIG.pl_link_cap_max_link_speed {8.0_GT/s} \
    CONFIG.xdma_axi_intf_mm {AXI_Memory_Mapped}] $xdma
connect_bd_net $pcie_ref_port [get_bd_pins xdma_0/sys_clk]
connect_bd_net $pcie_gt_port [get_bd_pins xdma_0/sys_clk_gt]
connect_bd_net $reset_port [get_bd_pins xdma_0/sys_rst_n]
set pcie_external [make_bd_intf_pins_external [get_bd_intf_pins xdma_0/pcie_mgt]]
set_property name pci_exp $pcie_external

set hbm [create_bd_cell -type ip -vlnv xilinx.com:ip:hbm:1.0 hbm_0]
set_property -dict [list \
    CONFIG.USER_APB_EN {false} \
    CONFIG.USER_CLK_SEL_LIST0 {AXI_00_ACLK} \
    CONFIG.USER_HBM_DENSITY {8GB} \
    CONFIG.USER_HBM_STACK {1} \
    CONFIG.USER_MC_ENABLE_APB_01 {FALSE} \
    CONFIG.USER_SWITCH_ENABLE_01 {FALSE} \
    CONFIG.USER_XSDB_INTF_EN {TRUE}] $hbm
connect_bd_net $hbm_ref_port [get_bd_pins hbm_0/HBM_REF_CLK_0]
connect_bd_net [get_bd_pins clk_wiz_0/clk_out1] [get_bd_pins hbm_0/APB_0_PCLK]
connect_bd_net [get_bd_pins rst_100/peripheral_aresetn] [get_bd_pins hbm_0/APB_0_PRESET_N]
connect_bd_net [get_bd_pins clk_wiz_0/clk_out2] [get_bd_pins hbm_0/AXI_00_ACLK]
connect_bd_net [get_bd_pins rst_225/peripheral_aresetn] [get_bd_pins hbm_0/AXI_00_ARESET_N]

set xdma_data_cc [create_bd_cell -type ip -vlnv xilinx.com:ip:axi_clock_converter:2.1 xdma_data_cc]
connect_bd_intf_net [get_bd_intf_pins xdma_0/M_AXI] [get_bd_intf_pins xdma_data_cc/S_AXI]
connect_bd_net [get_bd_pins xdma_0/axi_aclk] [get_bd_pins xdma_data_cc/s_axi_aclk]
connect_bd_net [get_bd_pins xdma_0/axi_aresetn] [get_bd_pins xdma_data_cc/s_axi_aresetn]
connect_bd_net [get_bd_pins clk_wiz_0/clk_out2] [get_bd_pins xdma_data_cc/m_axi_aclk]
connect_bd_net [get_bd_pins rst_225/peripheral_aresetn] [get_bd_pins xdma_data_cc/m_axi_aresetn]

set data_sc [create_bd_cell -type ip -vlnv xilinx.com:ip:smartconnect:1.0 data_smartconnect]
set_property -dict [list CONFIG.NUM_SI [expr {$include_fsa ? 2 : 1}] CONFIG.NUM_MI {1}] $data_sc
connect_bd_net [get_bd_pins clk_wiz_0/clk_out2] [get_bd_pins data_smartconnect/aclk]
connect_bd_net [get_bd_pins rst_225/interconnect_aresetn] [get_bd_pins data_smartconnect/aresetn]
connect_bd_intf_net [get_bd_intf_pins xdma_data_cc/M_AXI] [get_bd_intf_pins data_smartconnect/S00_AXI]
connect_bd_intf_net [get_bd_intf_pins data_smartconnect/M00_AXI] [get_bd_intf_pins hbm_0/AXI_00]

set axil_cc [create_bd_cell -type ip -vlnv xilinx.com:ip:axi_clock_converter:2.1 axil_cc]
set_property -dict [list CONFIG.PROTOCOL {AXI4LITE} CONFIG.ADDR_WIDTH {32} CONFIG.DATA_WIDTH {32}] $axil_cc
connect_bd_intf_net [get_bd_intf_pins xdma_0/M_AXI_LITE] [get_bd_intf_pins axil_cc/S_AXI]
connect_bd_net [get_bd_pins xdma_0/axi_aclk] [get_bd_pins axil_cc/s_axi_aclk]
connect_bd_net [get_bd_pins xdma_0/axi_aresetn] [get_bd_pins axil_cc/s_axi_aresetn]
connect_bd_net [get_bd_pins clk_wiz_0/clk_out1] [get_bd_pins axil_cc/m_axi_aclk]
connect_bd_net [get_bd_pins rst_100/peripheral_aresetn] [get_bd_pins axil_cc/m_axi_aresetn]

set axil_sc [create_bd_cell -type ip -vlnv xilinx.com:ip:smartconnect:1.0 axil_smartconnect]
set_property -dict [list CONFIG.NUM_SI {1} CONFIG.NUM_MI [expr {$include_fsa ? 2 : 1}]] $axil_sc
connect_bd_net [get_bd_pins clk_wiz_0/clk_out1] [get_bd_pins axil_smartconnect/aclk]
connect_bd_net [get_bd_pins rst_100/interconnect_aresetn] [get_bd_pins axil_smartconnect/aresetn]
connect_bd_intf_net [get_bd_intf_pins axil_cc/M_AXI] [get_bd_intf_pins axil_smartconnect/S00_AXI]

set status_regs [create_bd_cell -type module -reference u280_status_axil status_regs_0]
connect_bd_net [get_bd_pins clk_wiz_0/clk_out1] [get_bd_pins status_regs_0/aclk]
connect_bd_net [get_bd_pins rst_100/peripheral_aresetn] [get_bd_pins status_regs_0/aresetn]
connect_bd_net [get_bd_pins hbm_0/apb_complete_0] [get_bd_pins status_regs_0/hbm_init_done_async]
if {[llength [get_bd_pins -quiet hbm_0/DRAM_0_STAT_CATTRIP]] == 1} {
    connect_bd_net [get_bd_pins hbm_0/DRAM_0_STAT_CATTRIP] [get_bd_pins status_regs_0/hbm_cattrip_async]
} else {
    connect_bd_net [get_bd_pins const_zero/dout] [get_bd_pins status_regs_0/hbm_cattrip_async]
}
connect_bd_net [get_bd_pins xdma_0/user_lnk_up] [get_bd_pins status_regs_0/xdma_link_up_async]
connect_bd_intf_net [get_bd_intf_pins axil_smartconnect/M00_AXI] [get_bd_intf_pins status_regs_0/S_AXI]

if {$include_fsa} {
    set fsa_ip [create_bd_cell -type ip -vlnv $hls_ip_vlnv fsa_dma_top_0]
    connect_bd_net [get_bd_pins clk_wiz_0/clk_out1] [get_bd_pins fsa_dma_top_0/ap_clk]
    connect_bd_net [get_bd_pins rst_100/peripheral_aresetn] [get_bd_pins fsa_dma_top_0/ap_rst_n]
    connect_bd_intf_net [get_bd_intf_pins axil_smartconnect/M01_AXI] [get_bd_intf_pins fsa_dma_top_0/s_axi_control]

    set fsa_data_cc [create_bd_cell -type ip -vlnv xilinx.com:ip:axi_clock_converter:2.1 fsa_data_cc]
    connect_bd_intf_net [get_bd_intf_pins fsa_dma_top_0/m_axi_gmem] [get_bd_intf_pins fsa_data_cc/S_AXI]
    connect_bd_net [get_bd_pins clk_wiz_0/clk_out1] [get_bd_pins fsa_data_cc/s_axi_aclk]
    connect_bd_net [get_bd_pins rst_100/peripheral_aresetn] [get_bd_pins fsa_data_cc/s_axi_aresetn]
    connect_bd_net [get_bd_pins clk_wiz_0/clk_out2] [get_bd_pins fsa_data_cc/m_axi_aclk]
    connect_bd_net [get_bd_pins rst_225/peripheral_aresetn] [get_bd_pins fsa_data_cc/m_axi_aresetn]
    connect_bd_intf_net [get_bd_intf_pins fsa_data_cc/M_AXI] [get_bd_intf_pins data_smartconnect/S01_AXI]
}

set ila [create_bd_cell -type ip -vlnv xilinx.com:ip:ila:6.2 status_ila]
set_property -dict [list CONFIG.C_NUM_OF_PROBES {6} CONFIG.C_DATA_DEPTH {4096}] $ila
connect_bd_net [get_bd_pins clk_wiz_0/clk_out1] [get_bd_pins status_ila/clk]
connect_bd_net [get_bd_pins hbm_0/apb_complete_0] [get_bd_pins status_ila/probe0]
connect_bd_net [get_bd_pins xdma_0/user_lnk_up] [get_bd_pins status_ila/probe1]
connect_bd_net [get_bd_pins rst_100/peripheral_aresetn] [get_bd_pins status_ila/probe2]
connect_bd_net [get_bd_pins rst_225/peripheral_aresetn] [get_bd_pins status_ila/probe3]
if {$include_fsa} {
    connect_bd_net [get_bd_pins fsa_dma_top_0/interrupt] [get_bd_pins status_ila/probe4]
} else {
    connect_bd_net [get_bd_pins const_zero/dout] [get_bd_pins status_ila/probe4]
}
if {[llength [get_bd_pins -quiet hbm_0/DRAM_0_STAT_CATTRIP]] == 1} {
    connect_bd_net [get_bd_pins hbm_0/DRAM_0_STAT_CATTRIP] [get_bd_pins status_ila/probe5]
} else {
    connect_bd_net [get_bd_pins const_zero/dout] [get_bd_pins status_ila/probe5]
}

set hbm_seg [first_matching_addr_seg [list "*/HBM_MEM00" "*HBM_MEM*"] "HBM AXI_00"]
assign_bd_address -force -offset 0x00000000 -range $hbm_range_bytes \
    -target_address_space [get_bd_addr_spaces xdma_0/M_AXI] $hbm_seg

set status_seg [first_matching_addr_seg [list "status_regs_0/S_AXI/Reg" "*status_regs_0*/Reg"] "status registers"]
assign_bd_address -force -offset $status_ctrl_base -range 0x00010000 \
    -target_address_space [get_bd_addr_spaces xdma_0/M_AXI_LITE] $status_seg

if {$include_fsa} {
    set fsa_ctrl_seg [first_matching_addr_seg [list "fsa_dma_top_0/s_axi_control/Reg" "*fsa_dma_top_0*/Reg"] "FSA control"]
    assign_bd_address -force -offset $fsa_ctrl_base -range 0x00010000 \
        -target_address_space [get_bd_addr_spaces xdma_0/M_AXI_LITE] $fsa_ctrl_seg
    assign_bd_address -force -offset 0x00000000 -range $hbm_range_bytes \
        -target_address_space [get_bd_addr_spaces fsa_dma_top_0/m_axi_gmem] $hbm_seg
}

validate_bd_design
save_bd_design

set bd_file [require_one [get_files -quiet u280_system_bd.bd] "u280_system_bd.bd"]
set wrapper_files [make_wrapper -files $bd_file -top]
add_files -norecurse $wrapper_files
set_property top u280_system_top [get_filesets sources_1]
update_compile_order -fileset sources_1
generate_target all $bd_file

redirect -file [file join $report_root $stage_name xdma_properties.txt] {
    report_property -all [get_bd_cells xdma_0]
}
redirect -file [file join $report_root $stage_name hbm_properties.txt] {
    report_property -all [get_bd_cells hbm_0]
}
report_ip_status -file [file join $report_root $stage_name ip_status.rpt]
write_bd_tcl -force [file join $report_root $stage_name recreate_bd.tcl]

puts "PROJECT_CREATED=[file join $project_dir ${project_name}.xpr]"
puts "NEXT=vivado -mode batch -source scripts/build_and_report.tcl -tclargs [file join $project_dir ${project_name}.xpr]"
close_project
