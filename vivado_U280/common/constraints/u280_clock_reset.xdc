set_property PACKAGE_PIN BJ43 [get_ports sys_clk_300_p]
set_property PACKAGE_PIN BJ44 [get_ports sys_clk_300_n]
set_property IOSTANDARD LVDS [get_ports {sys_clk_300_p sys_clk_300_n}]

set_property PACKAGE_PIN G31 [get_ports hbm_ref_clk_100_p]
set_property PACKAGE_PIN F31 [get_ports hbm_ref_clk_100_n]
set_property IOSTANDARD LVDS [get_ports {hbm_ref_clk_100_p hbm_ref_clk_100_n}]

set_property PACKAGE_PIN BH26 [get_ports pcie_perst_n]
set_property IOSTANDARD LVCMOS18 [get_ports pcie_perst_n]
set_property PULLUP TRUE [get_ports pcie_perst_n]
set_false_path -from [get_ports pcie_perst_n]

