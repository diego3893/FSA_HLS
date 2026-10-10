# Real NM37 package I/O; AXI boundaries are now internal timed paths.
set_property PACKAGE_PIN BH42 [get_ports board_clk_p]
set_property PACKAGE_PIN BJ42 [get_ports board_clk_n]
set_property IOSTANDARD DIFF_SSTL12 [get_ports {board_clk_p board_clk_n}]
# NM37 sheet 3: U7 LVDS reaches Bank65 through 100nF AC coupling.
# Split ODT supplies receiver-side bias and approximately 96ohm differential
# termination. The oscillator-side R31/R32 do not bias the FPGA pins.
set_property ODT RTT_48 [get_ports {board_clk_p board_clk_n}]
set_property PACKAGE_PIN BF2 [get_ports reset_n]
set_property IOSTANDARD LVCMOS18 [get_ports reset_n]
create_clock -name board_clk_100 -period 10.000 [get_ports board_clk_p]
# The physical push button is asynchronous. Proc_sys_reset controls release
# in each clock domain; no AXI/data path is excluded.
set_false_path -from [get_ports reset_n]
