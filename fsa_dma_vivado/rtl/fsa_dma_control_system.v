`timescale 1ns / 1ps

// Controller + exported HLS IP + small AXI RAM. This module is shared by the
// board top and behavioral simulation, so the same AXI wiring is exercised.
module fsa_dma_control_system (
    input  wire         clk,
    input  wire         rst,
    input  wire         run_test,

    output wire         test_busy,
    output wire         test_done,
    output wire         test_pass,
    output wire         test_fail,
    output wire [5:0]   debug_state,
    output wire [7:0]   debug_fail_code,
    output wire [7:0]   debug_status,
    output wire [5:0]   debug_check_index,
    output wire [31:0]  debug_timeout,

    output wire [6:0]   debug_axil_awaddr,
    output wire [5:0]   debug_axil_write_flags,
    output wire [6:0]   debug_axil_araddr,
    output wire [3:0]   debug_axil_read_flags,
    output wire [31:0]  debug_axil_rdata,
    output wire [63:0]  debug_m_axi_araddr,
    output wire [3:0]   debug_m_axi_read_flags,
    output wire [63:0]  debug_m_axi_awaddr,
    output wire [63:0]  debug_m_axi_wdata,
    output wire [5:0]   debug_m_axi_write_flags
);

    wire [6:0]  ctrl_awaddr;
    wire         ctrl_awvalid;
    wire         ctrl_awready;
    wire [31:0]  ctrl_wdata;
    wire [3:0]   ctrl_wstrb;
    wire         ctrl_wvalid;
    wire         ctrl_wready;
    wire [1:0]   ctrl_bresp;
    wire         ctrl_bvalid;
    wire         ctrl_bready;
    wire [6:0]   ctrl_araddr;
    wire         ctrl_arvalid;
    wire         ctrl_arready;
    wire [31:0]  ctrl_rdata;
    wire [1:0]   ctrl_rresp;
    wire         ctrl_rvalid;
    wire         ctrl_rready;

    wire         ram_dbg_we;
    wire [8:0]   ram_dbg_addr;
    wire [63:0]  ram_dbg_wdata;
    wire [63:0]  ram_dbg_rdata;
    wire         ram_protocol_error;

    wire         m_awvalid;
    wire         m_awready;
    wire [63:0]  m_awaddr;
    wire [0:0]   m_awid;
    wire [7:0]   m_awlen;
    wire [2:0]   m_awsize;
    wire [1:0]   m_awburst;
    wire [1:0]   m_awlock;
    wire [3:0]   m_awcache;
    wire [2:0]   m_awprot;
    wire [3:0]   m_awqos;
    wire [3:0]   m_awregion;
    wire [0:0]   m_awuser;
    wire         m_wvalid;
    wire         m_wready;
    wire [63:0]  m_wdata;
    wire [7:0]   m_wstrb;
    wire         m_wlast;
    wire [0:0]   m_wid;
    wire [0:0]   m_wuser;
    wire         m_bvalid;
    wire         m_bready;
    wire [1:0]   m_bresp;
    wire [0:0]   m_bid;

    wire         m_arvalid;
    wire         m_arready;
    wire [63:0]  m_araddr;
    wire [0:0]   m_arid;
    wire [7:0]   m_arlen;
    wire [2:0]   m_arsize;
    wire [1:0]   m_arburst;
    wire [1:0]   m_arlock;
    wire [3:0]   m_arcache;
    wire [2:0]   m_arprot;
    wire [3:0]   m_arqos;
    wire [3:0]   m_arregion;
    wire [0:0]   m_aruser;
    wire         m_rvalid;
    wire         m_rready;
    wire [63:0]  m_rdata;
    wire         m_rlast;
    wire [0:0]   m_rid;
    wire [1:0]   m_rresp;
    wire         hls_interrupt;

    assign debug_axil_awaddr = ctrl_awaddr;
    assign debug_axil_write_flags = {
        ctrl_bready, ctrl_bvalid, ctrl_wready,
        ctrl_wvalid, ctrl_awready, ctrl_awvalid
    };
    assign debug_axil_araddr = ctrl_araddr;
    assign debug_axil_read_flags = {
        ctrl_rready, ctrl_rvalid, ctrl_arready, ctrl_arvalid
    };
    assign debug_axil_rdata = ctrl_rdata;
    assign debug_m_axi_araddr = m_araddr;
    assign debug_m_axi_read_flags = {
        m_rready, m_rvalid, m_arready, m_arvalid
    };
    assign debug_m_axi_awaddr = m_awaddr;
    assign debug_m_axi_wdata = m_wdata;
    assign debug_m_axi_write_flags = {
        m_bready, m_bvalid, m_wready,
        m_wvalid, m_awready, m_awvalid
    };

    fsa_dma_test_controller u_test_controller (
        .clk                 (clk),
        .rst                 (rst),
        .run_test            (run_test),
        .m_axil_awaddr       (ctrl_awaddr),
        .m_axil_awvalid      (ctrl_awvalid),
        .m_axil_awready      (ctrl_awready),
        .m_axil_wdata        (ctrl_wdata),
        .m_axil_wstrb        (ctrl_wstrb),
        .m_axil_wvalid       (ctrl_wvalid),
        .m_axil_wready       (ctrl_wready),
        .m_axil_bresp        (ctrl_bresp),
        .m_axil_bvalid       (ctrl_bvalid),
        .m_axil_bready       (ctrl_bready),
        .m_axil_araddr       (ctrl_araddr),
        .m_axil_arvalid      (ctrl_arvalid),
        .m_axil_arready      (ctrl_arready),
        .m_axil_rdata        (ctrl_rdata),
        .m_axil_rresp        (ctrl_rresp),
        .m_axil_rvalid       (ctrl_rvalid),
        .m_axil_rready       (ctrl_rready),
        .ram_dbg_we          (ram_dbg_we),
        .ram_dbg_addr        (ram_dbg_addr),
        .ram_dbg_wdata       (ram_dbg_wdata),
        .ram_dbg_rdata       (ram_dbg_rdata),
        .ram_protocol_error  (ram_protocol_error),
        .test_busy           (test_busy),
        .test_done           (test_done),
        .test_pass           (test_pass),
        .test_fail           (test_fail),
        .fail_code           (debug_fail_code),
        .debug_state         (debug_state),
        .debug_check_index   (debug_check_index),
        .debug_status        (debug_status),
        .debug_timeout       (debug_timeout)
    );

    // Module name is produced by create_ip using hls_instance_name from the
    // generated project configuration.
    fsa_dma_top_0 u_fsa_dma_top (
        .ap_clk                  (clk),
        .ap_rst_n                (~rst),

        .m_axi_gmem_AWVALID      (m_awvalid),
        .m_axi_gmem_AWREADY      (m_awready),
        .m_axi_gmem_AWADDR       (m_awaddr),
        .m_axi_gmem_AWID         (m_awid),
        .m_axi_gmem_AWLEN        (m_awlen),
        .m_axi_gmem_AWSIZE       (m_awsize),
        .m_axi_gmem_AWBURST      (m_awburst),
        .m_axi_gmem_AWLOCK       (m_awlock),
        .m_axi_gmem_AWCACHE      (m_awcache),
        .m_axi_gmem_AWPROT       (m_awprot),
        .m_axi_gmem_AWQOS        (m_awqos),
        .m_axi_gmem_AWREGION     (m_awregion),
        .m_axi_gmem_AWUSER       (m_awuser),
        .m_axi_gmem_WVALID       (m_wvalid),
        .m_axi_gmem_WREADY       (m_wready),
        .m_axi_gmem_WDATA        (m_wdata),
        .m_axi_gmem_WSTRB        (m_wstrb),
        .m_axi_gmem_WLAST        (m_wlast),
        .m_axi_gmem_WID          (m_wid),
        .m_axi_gmem_WUSER        (m_wuser),
        .m_axi_gmem_ARVALID      (m_arvalid),
        .m_axi_gmem_ARREADY      (m_arready),
        .m_axi_gmem_ARADDR       (m_araddr),
        .m_axi_gmem_ARID         (m_arid),
        .m_axi_gmem_ARLEN        (m_arlen),
        .m_axi_gmem_ARSIZE       (m_arsize),
        .m_axi_gmem_ARBURST      (m_arburst),
        .m_axi_gmem_ARLOCK       (m_arlock),
        .m_axi_gmem_ARCACHE      (m_arcache),
        .m_axi_gmem_ARPROT       (m_arprot),
        .m_axi_gmem_ARQOS        (m_arqos),
        .m_axi_gmem_ARREGION     (m_arregion),
        .m_axi_gmem_ARUSER       (m_aruser),
        .m_axi_gmem_RVALID       (m_rvalid),
        .m_axi_gmem_RREADY       (m_rready),
        .m_axi_gmem_RDATA        (m_rdata),
        .m_axi_gmem_RLAST        (m_rlast),
        .m_axi_gmem_RID          (m_rid),
        .m_axi_gmem_RUSER        (1'b0),
        .m_axi_gmem_RRESP        (m_rresp),
        .m_axi_gmem_BVALID       (m_bvalid),
        .m_axi_gmem_BREADY       (m_bready),
        .m_axi_gmem_BRESP        (m_bresp),
        .m_axi_gmem_BID          (m_bid),
        .m_axi_gmem_BUSER        (1'b0),

        .s_axi_control_AWVALID   (ctrl_awvalid),
        .s_axi_control_AWREADY   (ctrl_awready),
        .s_axi_control_AWADDR    (ctrl_awaddr),
        .s_axi_control_WVALID    (ctrl_wvalid),
        .s_axi_control_WREADY    (ctrl_wready),
        .s_axi_control_WDATA     (ctrl_wdata),
        .s_axi_control_WSTRB     (ctrl_wstrb),
        .s_axi_control_ARVALID   (ctrl_arvalid),
        .s_axi_control_ARREADY   (ctrl_arready),
        .s_axi_control_ARADDR    (ctrl_araddr),
        .s_axi_control_RVALID    (ctrl_rvalid),
        .s_axi_control_RREADY    (ctrl_rready),
        .s_axi_control_RDATA     (ctrl_rdata),
        .s_axi_control_RRESP     (ctrl_rresp),
        .s_axi_control_BVALID    (ctrl_bvalid),
        .s_axi_control_BREADY    (ctrl_bready),
        .s_axi_control_BRESP     (ctrl_bresp),
        .interrupt               (hls_interrupt)
    );

    fsa_dma_axi_ram64 u_axi_ram (
        .clk            (clk),
        .rst            (rst),
        .s_axi_awvalid  (m_awvalid),
        .s_axi_awready  (m_awready),
        .s_axi_awaddr   (m_awaddr),
        .s_axi_awid     (m_awid),
        .s_axi_awlen    (m_awlen),
        .s_axi_awsize   (m_awsize),
        .s_axi_awburst  (m_awburst),
        .s_axi_wvalid   (m_wvalid),
        .s_axi_wready   (m_wready),
        .s_axi_wdata    (m_wdata),
        .s_axi_wstrb    (m_wstrb),
        .s_axi_wlast    (m_wlast),
        .s_axi_bvalid   (m_bvalid),
        .s_axi_bready   (m_bready),
        .s_axi_bresp    (m_bresp),
        .s_axi_bid      (m_bid),
        .s_axi_arvalid  (m_arvalid),
        .s_axi_arready  (m_arready),
        .s_axi_araddr   (m_araddr),
        .s_axi_arid     (m_arid),
        .s_axi_arlen    (m_arlen),
        .s_axi_arsize   (m_arsize),
        .s_axi_arburst  (m_arburst),
        .s_axi_rvalid   (m_rvalid),
        .s_axi_rready   (m_rready),
        .s_axi_rdata    (m_rdata),
        .s_axi_rlast    (m_rlast),
        .s_axi_rid      (m_rid),
        .s_axi_rresp    (m_rresp),
        .dbg_we         (ram_dbg_we),
        .dbg_addr       (ram_dbg_addr),
        .dbg_wdata      (ram_dbg_wdata),
        .dbg_rdata      (ram_dbg_rdata),
        .protocol_error (ram_protocol_error)
    );

    wire unused_hls_sideband = hls_interrupt ^ m_awlock[0] ^ m_awlock[1] ^
        ^m_awcache ^ ^m_awprot ^ ^m_awqos ^ ^m_awregion ^ ^m_awuser ^
        ^m_wid ^ ^m_wuser ^ ^m_arlock ^ ^m_arcache ^ ^m_arprot ^
        ^m_arqos ^ ^m_arregion ^ ^m_aruser;

endmodule

