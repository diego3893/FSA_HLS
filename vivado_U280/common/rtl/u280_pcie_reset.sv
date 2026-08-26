`timescale 1ns/1ps

// Recreates the verified FSA U280 fix: after every FPGA configuration, keep
// PCIe sys_rst_n low for a finite interval even when the host PERST# stayed high.
module u280_pcie_reset #(
    parameter integer RELEASE_CYCLES = 100000
) (
    input  wire clk_100,
    input  wire external_perst_n,
    output reg  pcie_sys_rst_n = 1'b0
);
    localparam integer COUNTER_WIDTH = $clog2(RELEASE_CYCLES + 1);
    reg [COUNTER_WIDTH-1:0] release_count = {COUNTER_WIDTH{1'b0}};

    always @(posedge clk_100 or negedge external_perst_n) begin
        if (!external_perst_n) begin
            release_count  <= {COUNTER_WIDTH{1'b0}};
            pcie_sys_rst_n <= 1'b0;
        end else if (!pcie_sys_rst_n) begin
            if (release_count == RELEASE_CYCLES - 1) begin
                pcie_sys_rst_n <= 1'b1;
            end else begin
                release_count <= release_count + 1'b1;
            end
        end
    end
endmodule

