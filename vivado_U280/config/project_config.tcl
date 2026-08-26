# Vivado 2024.2 U280 configuration shared by every stage.
set config_dir   [file normalize [file dirname [info script]]]
set package_dir  [file normalize [file join $config_dir ..]]
set fsa_hls_root [file normalize [file join $package_dir ..]]

set target_part  "xcu280-fsvh2892-2L-e"
set board_part   "xilinx.com:au280:part0:1.1"

set hls_ip_vlnv  "xilinx.com:hls:fsa_dma_top:1.0"
set hls_ip_repo   [file join $package_dir ip_repo]

set sys_clk_mhz       300.000
set fsa_clk_mhz       100.000
set hbm_axi_clk_mhz   225.000
set pcie_ref_clk_mhz  100.000
set xdma_axi_clk_mhz  250.000

set hbm_range_bytes   0x10000000
set fsa_ctrl_base     0x00000000
set status_ctrl_base  0x00010000

set q_buffer_base     0x00000000
set k_buffer_base     0x01000000
set v_buffer_base     0x02000000
set o_buffer_base     0x03000000

set common_rtl_dir    [file join $package_dir common rtl]
set common_xdc_dir    [file join $package_dir common constraints]
set build_root        [file join $package_dir build]
set report_root       [file join $package_dir reports]
