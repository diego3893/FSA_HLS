set stage_dir [file normalize [file dirname [info script]]]
set package_dir [file normalize [file join $stage_dir ..]]
source [file join $package_dir config project_config.tcl]
assert_required_vivado_version
set project_dir [file join $build_root 03_xdma_ip]
set example_dir [file join $build_root 03_xdma_example]
file mkdir $build_root
file mkdir [file join $report_root 03_xdma]

create_project -force fsa_u280_03_xdma_ip $project_dir -part $target_part
apply_u280_board_part_if_available
create_ip -vlnv xilinx.com:ip:xdma:4.1 -module_name xdma_0
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
    CONFIG.xdma_axi_intf_mm {AXI_Memory_Mapped}] [get_ips xdma_0]
generate_target all [get_ips xdma_0]
redirect -file [file join $report_root 03_xdma xdma_properties.txt] {
    report_property -all [get_ips xdma_0]
}
open_example_project -force -dir $example_dir [get_ips xdma_0]
puts "EXAMPLE_PROJECT_CREATED=$example_dir"
