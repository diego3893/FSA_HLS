`timescale 1ns/1ps

module u280_clock_reset_top (
    input wire sys_clk_300_p,
    input wire sys_clk_300_n,
    input wire hbm_ref_clk_100_p,
    input wire hbm_ref_clk_100_n,
    input wire pcie_perst_n
);
    wire sys_clk_300;
    wire hbm_ref_clk_100;
    wire clk_100;
    wire clk_225;
    wire clocks_locked;
    wire pcie_reset_released;

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

    u280_clk_wiz_0 u_clock_wizard (
        .clk_in1(sys_clk_300),
        .reset(~pcie_perst_n),
        .clk_out1(clk_100),
        .clk_out2(clk_225),
        .locked(clocks_locked)
    );

    u280_pcie_reset u_pcie_reset (
        .clk_100(hbm_ref_clk_100),
        .external_perst_n(pcie_perst_n),
        .pcie_sys_rst_n(pcie_reset_released)
    );

    (* mark_debug = "true", keep = "true" *) wire dbg_clk_100 = clk_100;
    (* mark_debug = "true", keep = "true" *) wire dbg_clk_225 = clk_225;
    (* mark_debug = "true", keep = "true" *) wire dbg_locked = clocks_locked;
    (* mark_debug = "true", keep = "true" *) wire dbg_pcie_reset_released = pcie_reset_released;
endmodule

