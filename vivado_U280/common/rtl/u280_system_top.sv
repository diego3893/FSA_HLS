`timescale 1ns/1ps

module u280_system_top (
    input  wire       sys_clk_300_p,
    input  wire       sys_clk_300_n,
    input  wire       hbm_ref_clk_100_p,
    input  wire       hbm_ref_clk_100_n,
    input  wire       pcie_ref_clk_100_p,
    input  wire       pcie_ref_clk_100_n,
    input  wire       pcie_perst_n,
    input  wire [7:0] pci_exp_rxp,
    input  wire [7:0] pci_exp_rxn,
    output wire [7:0] pci_exp_txp,
    output wire [7:0] pci_exp_txn
);
    wire sys_clk_300;
    wire hbm_ref_clk_100;
    wire pcie_ref_clk_100;
    wire pcie_ref_clk_gt;
    wire pcie_sys_rst_n;

    IBUFDS u_sys_clk_ibuf (
        .I(sys_clk_300_p),
        .IB(sys_clk_300_n),
        .O(sys_clk_300)
    );

    IBUFDS u_hbm_ref_ibuf (
        .I(hbm_ref_clk_100_p),
        .IB(hbm_ref_clk_100_n),
        .O(hbm_ref_clk_100)
    );

    IBUFDS_GTE4 u_pcie_refclk_ibuf (
        .I(pcie_ref_clk_100_p),
        .IB(pcie_ref_clk_100_n),
        .CEB(1'b0),
        .O(pcie_ref_clk_gt),
        .ODIV2(pcie_ref_clk_100)
    );

    u280_pcie_reset u_pcie_reset (
        .clk_100(pcie_ref_clk_100),
        .external_perst_n(pcie_perst_n),
        .pcie_sys_rst_n(pcie_sys_rst_n)
    );

    u280_system_bd_wrapper u_bd (
        .fabric_reset(~pcie_sys_rst_n),
        .hbm_ref_clk_100(hbm_ref_clk_100),
        .pci_exp_rxn(pci_exp_rxn),
        .pci_exp_rxp(pci_exp_rxp),
        .pci_exp_txn(pci_exp_txn),
        .pci_exp_txp(pci_exp_txp),
        .pcie_ref_clk_100(pcie_ref_clk_100),
        .pcie_ref_clk_gt(pcie_ref_clk_gt),
        .pcie_sys_rst_n(pcie_sys_rst_n),
        .sys_clk_300(sys_clk_300)
    );
endmodule

