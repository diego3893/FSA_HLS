# U280 pins verified by the original FSA U280 shell.
# Clock constraints for generated clocks are supplied by Clocking Wizard,
# XDMA and HBM IP. Do not add duplicate create_clock commands here.

set_property PACKAGE_PIN BJ43 [get_ports sys_clk_300_p]
set_property PACKAGE_PIN BJ44 [get_ports sys_clk_300_n]
set_property IOSTANDARD LVDS [get_ports {sys_clk_300_p sys_clk_300_n}]

set_property PACKAGE_PIN G31 [get_ports hbm_ref_clk_100_p]
set_property PACKAGE_PIN F31 [get_ports hbm_ref_clk_100_n]
set_property IOSTANDARD LVDS [get_ports {hbm_ref_clk_100_p hbm_ref_clk_100_n}]

set_property PACKAGE_PIN AR15 [get_ports pcie_ref_clk_100_p]
set_property PACKAGE_PIN AR14 [get_ports pcie_ref_clk_100_n]

set_property PACKAGE_PIN BH26 [get_ports pcie_perst_n]
set_property IOSTANDARD LVCMOS18 [get_ports pcie_perst_n]
set_property PULLUP TRUE [get_ports pcie_perst_n]
set_false_path -from [get_ports pcie_perst_n]

set_property PACKAGE_PIN AL2  [get_ports {pci_exp_rxp[0]}]
set_property PACKAGE_PIN AM4  [get_ports {pci_exp_rxp[1]}]
set_property PACKAGE_PIN AN6  [get_ports {pci_exp_rxp[2]}]
set_property PACKAGE_PIN AN2  [get_ports {pci_exp_rxp[3]}]
set_property PACKAGE_PIN AP4  [get_ports {pci_exp_rxp[4]}]
set_property PACKAGE_PIN AR2  [get_ports {pci_exp_rxp[5]}]
set_property PACKAGE_PIN AT4  [get_ports {pci_exp_rxp[6]}]
set_property PACKAGE_PIN AU2  [get_ports {pci_exp_rxp[7]}]

set_property PACKAGE_PIN AL1  [get_ports {pci_exp_rxn[0]}]
set_property PACKAGE_PIN AM3  [get_ports {pci_exp_rxn[1]}]
set_property PACKAGE_PIN AN5  [get_ports {pci_exp_rxn[2]}]
set_property PACKAGE_PIN AN1  [get_ports {pci_exp_rxn[3]}]
set_property PACKAGE_PIN AP3  [get_ports {pci_exp_rxn[4]}]
set_property PACKAGE_PIN AR1  [get_ports {pci_exp_rxn[5]}]
set_property PACKAGE_PIN AT3  [get_ports {pci_exp_rxn[6]}]
set_property PACKAGE_PIN AU1  [get_ports {pci_exp_rxn[7]}]

set_property PACKAGE_PIN AL11 [get_ports {pci_exp_txp[0]}]
set_property PACKAGE_PIN AM9  [get_ports {pci_exp_txp[1]}]
set_property PACKAGE_PIN AN11 [get_ports {pci_exp_txp[2]}]
set_property PACKAGE_PIN AP9  [get_ports {pci_exp_txp[3]}]
set_property PACKAGE_PIN AR11 [get_ports {pci_exp_txp[4]}]
set_property PACKAGE_PIN AR7  [get_ports {pci_exp_txp[5]}]
set_property PACKAGE_PIN AT9  [get_ports {pci_exp_txp[6]}]
set_property PACKAGE_PIN AU11 [get_ports {pci_exp_txp[7]}]

set_property PACKAGE_PIN AL10 [get_ports {pci_exp_txn[0]}]
set_property PACKAGE_PIN AM8  [get_ports {pci_exp_txn[1]}]
set_property PACKAGE_PIN AN10 [get_ports {pci_exp_txn[2]}]
set_property PACKAGE_PIN AP8  [get_ports {pci_exp_txn[3]}]
set_property PACKAGE_PIN AR10 [get_ports {pci_exp_txn[4]}]
set_property PACKAGE_PIN AR6  [get_ports {pci_exp_txn[5]}]
set_property PACKAGE_PIN AT8  [get_ports {pci_exp_txn[6]}]
set_property PACKAGE_PIN AU10 [get_ports {pci_exp_txn[7]}]

