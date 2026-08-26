`timescale 1ns/1ps

module u280_status_axil (
    (* X_INTERFACE_PARAMETER = "XIL_INTERFACENAME aclk, ASSOCIATED_BUSIF S_AXI, ASSOCIATED_RESET aresetn" *)
    (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 aclk CLK" *)
    input  wire        aclk,
    (* X_INTERFACE_PARAMETER = "XIL_INTERFACENAME aresetn, POLARITY ACTIVE_LOW" *)
    (* X_INTERFACE_INFO = "xilinx.com:signal:reset:1.0 aresetn RST" *)
    input  wire        aresetn,
    input  wire        hbm_init_done_async,
    input  wire        hbm_cattrip_async,
    input  wire        xdma_link_up_async,

    (* X_INTERFACE_PARAMETER = "XIL_INTERFACENAME S_AXI, PROTOCOL AXI4LITE, DATA_WIDTH 32, ADDR_WIDTH 12, READ_WRITE_MODE READ_WRITE" *)
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI AWADDR" *)
    input  wire [11:0] s_axi_awaddr,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI AWVALID" *)
    input  wire        s_axi_awvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI AWREADY" *)
    output wire        s_axi_awready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI WDATA" *)
    input  wire [31:0] s_axi_wdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI WSTRB" *)
    input  wire [3:0]  s_axi_wstrb,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI WVALID" *)
    input  wire        s_axi_wvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI WREADY" *)
    output wire        s_axi_wready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI BRESP" *)
    output wire [1:0]  s_axi_bresp,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI BVALID" *)
    output reg         s_axi_bvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI BREADY" *)
    input  wire        s_axi_bready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI ARADDR" *)
    input  wire [11:0] s_axi_araddr,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI ARVALID" *)
    input  wire        s_axi_arvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI ARREADY" *)
    output wire        s_axi_arready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI RDATA" *)
    output reg  [31:0] s_axi_rdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI RRESP" *)
    output wire [1:0]  s_axi_rresp,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI RVALID" *)
    output reg         s_axi_rvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI RREADY" *)
    input  wire        s_axi_rready
);
    reg hbm_done_meta;
    reg hbm_done_sync;
    reg cattrip_meta;
    reg cattrip_sync;
    reg link_meta;
    reg link_sync;
    reg aw_seen;
    reg w_seen;

    assign s_axi_awready = aresetn && !aw_seen && !s_axi_bvalid;
    assign s_axi_wready  = aresetn && !w_seen && !s_axi_bvalid;
    assign s_axi_bresp   = 2'b00;
    assign s_axi_arready = aresetn && !s_axi_rvalid;
    assign s_axi_rresp   = 2'b00;

    always @(posedge aclk) begin
        if (!aresetn) begin
            hbm_done_meta <= 1'b0;
            hbm_done_sync <= 1'b0;
            cattrip_meta   <= 1'b0;
            cattrip_sync   <= 1'b0;
            link_meta      <= 1'b0;
            link_sync      <= 1'b0;
            aw_seen        <= 1'b0;
            w_seen         <= 1'b0;
            s_axi_bvalid   <= 1'b0;
            s_axi_rvalid   <= 1'b0;
            s_axi_rdata    <= 32'b0;
        end else begin
            hbm_done_meta <= hbm_init_done_async;
            hbm_done_sync <= hbm_done_meta;
            cattrip_meta   <= hbm_cattrip_async;
            cattrip_sync   <= cattrip_meta;
            link_meta      <= xdma_link_up_async;
            link_sync      <= link_meta;

            if (s_axi_awready && s_axi_awvalid)
                aw_seen <= 1'b1;
            if (s_axi_wready && s_axi_wvalid)
                w_seen <= 1'b1;
            if ((aw_seen || (s_axi_awready && s_axi_awvalid)) &&
                (w_seen || (s_axi_wready && s_axi_wvalid)) && !s_axi_bvalid) begin
                s_axi_bvalid <= 1'b1;
                aw_seen <= 1'b0;
                w_seen <= 1'b0;
            end else if (s_axi_bvalid && s_axi_bready) begin
                s_axi_bvalid <= 1'b0;
            end

            if (s_axi_arready && s_axi_arvalid) begin
                s_axi_rvalid <= 1'b1;
                case (s_axi_araddr[5:2])
                    4'h0: s_axi_rdata <= 32'h46534131; // "FSA1"
                    4'h1: s_axi_rdata <= {29'b0, cattrip_sync, link_sync, hbm_done_sync};
                    4'h2: s_axi_rdata <= 32'h00010000; // package register version 1.0
                    4'h3: s_axi_rdata <= 32'h10000000; // AXI_00 legal bytes
                    default: s_axi_rdata <= 32'b0;
                endcase
            end else if (s_axi_rvalid && s_axi_rready) begin
                s_axi_rvalid <= 1'b0;
            end
        end
    end

    wire unused_write_inputs = &{1'b0, s_axi_awaddr, s_axi_wdata, s_axi_wstrb};
endmodule
