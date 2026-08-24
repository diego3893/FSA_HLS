`timescale 1ns / 1ps

// Small synthesizable AXI4 memory used only by the fixed-vector board test.
// It accepts one read burst and one write burst at a time. Backpressure is
// legal AXI behavior and is sufficient for the exported fsa_dma_top master.
// The debug port lets the test controller preload Q/K/V and inspect O without
// any host-side DMA or software.
module fsa_dma_axi_ram64 #(
    parameter ADDR_WIDTH = 12,
    parameter WORDS = 512
) (
    input  wire         clk,
    input  wire         rst,

    input  wire         s_axi_awvalid,
    output wire         s_axi_awready,
    input  wire [63:0]  s_axi_awaddr,
    input  wire [0:0]   s_axi_awid,
    input  wire [7:0]   s_axi_awlen,
    input  wire [2:0]   s_axi_awsize,
    input  wire [1:0]   s_axi_awburst,

    input  wire         s_axi_wvalid,
    output wire         s_axi_wready,
    input  wire [63:0]  s_axi_wdata,
    input  wire [7:0]   s_axi_wstrb,
    input  wire         s_axi_wlast,

    output reg          s_axi_bvalid,
    input  wire         s_axi_bready,
    output reg  [1:0]   s_axi_bresp,
    output reg  [0:0]   s_axi_bid,

    input  wire         s_axi_arvalid,
    output wire         s_axi_arready,
    input  wire [63:0]  s_axi_araddr,
    input  wire [0:0]   s_axi_arid,
    input  wire [7:0]   s_axi_arlen,
    input  wire [2:0]   s_axi_arsize,
    input  wire [1:0]   s_axi_arburst,

    output reg          s_axi_rvalid,
    input  wire         s_axi_rready,
    output reg  [63:0]  s_axi_rdata,
    output reg          s_axi_rlast,
    output reg  [0:0]   s_axi_rid,
    output reg  [1:0]   s_axi_rresp,

    input  wire         dbg_we,
    input  wire [8:0]   dbg_addr,
    input  wire [63:0]  dbg_wdata,
    output wire [63:0]  dbg_rdata,

    output reg          protocol_error
);

    reg [63:0] memory [0:WORDS-1];

    reg        write_active;
    reg [63:0] write_addr;
    reg [7:0]  write_len;
    reg [7:0]  write_count;
    reg [2:0]  write_size;
    reg [1:0]  write_burst;

    reg        read_active;
    reg [63:0] read_addr;
    reg [7:0]  read_len;
    reg [7:0]  read_count;
    reg [2:0]  read_size;
    reg [1:0]  read_burst;

    integer byte_index;

    wire write_addr_ok = (write_addr[63:ADDR_WIDTH] == 0);
    wire read_addr_ok  = (read_addr[63:ADDR_WIDTH] == 0);
    wire [8:0] write_word_index = write_addr[ADDR_WIDTH-1:3];
    wire [8:0] read_word_index  = read_addr[ADDR_WIDTH-1:3];
    wire [63:0] read_next_addr =
        read_addr + (64'd1 << read_size);
    wire read_next_addr_ok =
        (read_next_addr[63:ADDR_WIDTH] == 0);
    wire [8:0] read_next_word_index =
        read_next_addr[ADDR_WIDTH-1:3];

    assign s_axi_awready = !write_active && !s_axi_bvalid;
    assign s_axi_wready  = write_active && !s_axi_bvalid;
    assign s_axi_arready = !read_active && !s_axi_rvalid;
    assign dbg_rdata = memory[dbg_addr];

    always @(posedge clk) begin
        if (rst) begin
            write_active  <= 1'b0;
            write_addr    <= 64'd0;
            write_len     <= 8'd0;
            write_count   <= 8'd0;
            write_size    <= 3'd0;
            write_burst   <= 2'd0;
            s_axi_bvalid  <= 1'b0;
            s_axi_bresp   <= 2'b00;
            s_axi_bid     <= 1'b0;

            read_active   <= 1'b0;
            read_addr     <= 64'd0;
            read_len      <= 8'd0;
            read_count    <= 8'd0;
            read_size     <= 3'd0;
            read_burst    <= 2'd0;
            s_axi_rvalid  <= 1'b0;
            s_axi_rdata   <= 64'd0;
            s_axi_rlast   <= 1'b0;
            s_axi_rid     <= 1'b0;
            s_axi_rresp   <= 2'b00;
            protocol_error <= 1'b0;
        end else begin
            if (dbg_we && !write_active) begin
                memory[dbg_addr] <= dbg_wdata;
            end

            if (s_axi_awvalid && s_axi_awready) begin
                write_active <= 1'b1;
                write_addr   <= s_axi_awaddr;
                write_len    <= s_axi_awlen;
                write_count  <= 8'd0;
                write_size   <= s_axi_awsize;
                write_burst  <= s_axi_awburst;
                s_axi_bid    <= s_axi_awid;
                if (s_axi_awsize != 3'd3 || s_axi_awburst != 2'b01 ||
                    s_axi_awaddr[63:ADDR_WIDTH] != 0 ||
                    s_axi_awaddr[2:0] != 3'b000) begin
                    protocol_error <= 1'b1;
                end
            end

            if (s_axi_wvalid && s_axi_wready) begin
                if (write_addr_ok && write_word_index < WORDS) begin
                    for (byte_index = 0; byte_index < 8; byte_index = byte_index + 1) begin
                        if (s_axi_wstrb[byte_index]) begin
                            memory[write_word_index][8*byte_index +: 8]
                                <= s_axi_wdata[8*byte_index +: 8];
                        end
                    end
                end else begin
                    protocol_error <= 1'b1;
                end

                if (s_axi_wlast != (write_count == write_len)) begin
                    protocol_error <= 1'b1;
                end

                if (s_axi_wlast || write_count == write_len) begin
                    write_active <= 1'b0;
                    s_axi_bvalid <= 1'b1;
                    s_axi_bresp  <= write_addr_ok ? 2'b00 : 2'b10;
                end else begin
                    write_count <= write_count + 1'b1;
                    if (write_burst == 2'b01) begin
                        write_addr <= write_addr + (64'd1 << write_size);
                    end
                end
            end

            if (s_axi_bvalid && s_axi_bready) begin
                s_axi_bvalid <= 1'b0;
            end

            if (s_axi_arvalid && s_axi_arready) begin
                read_active <= 1'b1;
                read_addr   <= s_axi_araddr;
                read_len    <= s_axi_arlen;
                read_count  <= 8'd0;
                read_size   <= s_axi_arsize;
                read_burst  <= s_axi_arburst;
                s_axi_rid   <= s_axi_arid;
                s_axi_rvalid <= 1'b1;
                s_axi_rlast <= (s_axi_arlen == 0);

                if (s_axi_araddr[63:ADDR_WIDTH] == 0 &&
                    s_axi_araddr[ADDR_WIDTH-1:3] < WORDS) begin
                    s_axi_rdata <= memory[s_axi_araddr[ADDR_WIDTH-1:3]];
                    s_axi_rresp <= 2'b00;
                end else begin
                    s_axi_rdata <= 64'd0;
                    s_axi_rresp <= 2'b10;
                    protocol_error <= 1'b1;
                end

                if (s_axi_arsize != 3'd3 || s_axi_arburst != 2'b01 ||
                    s_axi_araddr[2:0] != 3'b000) begin
                    protocol_error <= 1'b1;
                end
            end

            if (s_axi_rvalid && s_axi_rready) begin
                if (s_axi_rlast) begin
                    s_axi_rvalid <= 1'b0;
                    s_axi_rlast  <= 1'b0;
                    read_active  <= 1'b0;
                end else begin
                    read_count <= read_count + 1'b1;
                    if (read_burst == 2'b01) begin
                        read_addr <= read_addr + (64'd1 << read_size);
                    end

                    if (read_next_addr_ok && read_next_word_index < WORDS) begin
                        s_axi_rdata <= memory[read_next_word_index];
                        s_axi_rresp <= 2'b00;
                    end else begin
                        s_axi_rdata <= 64'd0;
                        s_axi_rresp <= 2'b10;
                        protocol_error <= 1'b1;
                    end
                    s_axi_rlast <= (read_count + 1'b1 == read_len);
                end
            end
        end
    end

endmodule
