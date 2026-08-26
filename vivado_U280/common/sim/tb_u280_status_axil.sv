`timescale 1ns/1ps

module tb_u280_status_axil;
    reg clk = 1'b0;
    reg resetn = 1'b0;
    reg hbm_done = 1'b0;
    reg cattrip = 1'b0;
    reg link_up = 1'b0;
    reg [11:0] awaddr = 12'b0;
    reg awvalid = 1'b0;
    wire awready;
    reg [31:0] wdata = 32'b0;
    reg [3:0] wstrb = 4'b0;
    reg wvalid = 1'b0;
    wire wready;
    wire [1:0] bresp;
    wire bvalid;
    reg bready = 1'b1;
    reg [11:0] araddr = 12'b0;
    reg arvalid = 1'b0;
    wire arready;
    wire [31:0] rdata;
    wire [1:0] rresp;
    wire rvalid;
    reg rready = 1'b1;
    integer failures = 0;

    always #5 clk = ~clk;

    u280_status_axil dut (
        .aclk(clk), .aresetn(resetn),
        .hbm_init_done_async(hbm_done),
        .hbm_cattrip_async(cattrip),
        .xdma_link_up_async(link_up),
        .s_axi_awaddr(awaddr), .s_axi_awvalid(awvalid), .s_axi_awready(awready),
        .s_axi_wdata(wdata), .s_axi_wstrb(wstrb), .s_axi_wvalid(wvalid), .s_axi_wready(wready),
        .s_axi_bresp(bresp), .s_axi_bvalid(bvalid), .s_axi_bready(bready),
        .s_axi_araddr(araddr), .s_axi_arvalid(arvalid), .s_axi_arready(arready),
        .s_axi_rdata(rdata), .s_axi_rresp(rresp), .s_axi_rvalid(rvalid), .s_axi_rready(rready)
    );

    task read_expect(input [11:0] address, input [31:0] expected);
        begin
            @(posedge clk);
            araddr <= address;
            arvalid <= 1'b1;
            while (!arready) @(posedge clk);
            @(posedge clk);
            arvalid <= 1'b0;
            while (!rvalid) @(posedge clk);
            if (rdata !== expected || rresp !== 2'b00) begin
                $display("[FAIL] addr=%h expected=%h actual=%h resp=%b", address, expected, rdata, rresp);
                failures = failures + 1;
            end
        end
    endtask

    initial begin
        repeat (5) @(posedge clk);
        resetn <= 1'b1;
        repeat (4) @(posedge clk);
        read_expect(12'h000, 32'h46534131);
        read_expect(12'h004, 32'h00000000);
        hbm_done <= 1'b1;
        link_up <= 1'b1;
        repeat (4) @(posedge clk);
        read_expect(12'h004, 32'h00000003);
        cattrip <= 1'b1;
        repeat (4) @(posedge clk);
        read_expect(12'h004, 32'h00000007);
        read_expect(12'h00c, 32'h10000000);
        if (failures == 0)
            $display("[PASS] u280_status_axil register test");
        else
            $fatal(1, "[FAIL] u280_status_axil failures=%0d", failures);
        $finish;
    end

    initial begin
        #100000;
        $fatal(1, "[FAIL] simulation timeout");
    end
endmodule

