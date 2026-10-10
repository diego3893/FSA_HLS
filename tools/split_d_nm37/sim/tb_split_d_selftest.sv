`timescale 1ns/1ps
module tb_split_d_selftest;
    reg clk;
    reg reset_n;
    reg run_test;
    reg memory_ready;
    reg stress_enable;
    wire [31:0] m_axi_awaddr;
    wire [7:0] m_axi_awlen;
    wire [2:0] m_axi_awsize;
    wire [1:0] m_axi_awburst;
    wire m_axi_awlock;
    wire [3:0] m_axi_awcache;
    wire [2:0] m_axi_awprot;
    wire [3:0] m_axi_awqos;
    wire m_axi_awvalid;
    reg m_axi_awready;
    wire [63:0] m_axi_wdata;
    wire [7:0] m_axi_wstrb;
    wire m_axi_wlast;
    wire m_axi_wvalid;
    reg m_axi_wready;
    reg [1:0] m_axi_bresp;
    reg m_axi_bvalid;
    wire m_axi_bready;
    wire [31:0] m_axi_araddr;
    wire [7:0] m_axi_arlen;
    wire [2:0] m_axi_arsize;
    wire [1:0] m_axi_arburst;
    wire m_axi_arlock;
    wire [3:0] m_axi_arcache;
    wire [2:0] m_axi_arprot;
    wire [3:0] m_axi_arqos;
    wire m_axi_arvalid;
    reg m_axi_arready;
    reg [63:0] m_axi_rdata;
    reg [1:0] m_axi_rresp;
    reg m_axi_rlast;
    reg m_axi_rvalid;
    wire m_axi_rready;
    wire [31:0] m_axil_awaddr;
    wire [2:0] m_axil_awprot;
    wire m_axil_awvalid;
    reg m_axil_awready;
    wire [31:0] m_axil_wdata;
    wire [3:0] m_axil_wstrb;
    wire m_axil_wvalid;
    reg m_axil_wready;
    reg [1:0] m_axil_bresp;
    reg m_axil_bvalid;
    wire m_axil_bready;
    wire [31:0] m_axil_araddr;
    wire [2:0] m_axil_arprot;
    wire m_axil_arvalid;
    reg m_axil_arready;
    reg [31:0] m_axil_rdata;
    reg [1:0] m_axil_rresp;
    reg m_axil_rvalid;
    wire m_axil_rready;
    wire test_busy;
    wire test_done;
    wire test_pass;
    wire test_fail;
    wire [7:0] fail_code;
    wire [5:0] cases_done;
    wire [31:0] last_case_cycles;
    wire [12:0] debug_pc;
    wire [3:0] debug_state;
    wire [63:0] actual_word;
    split_d_selftest #(.ROM_FILE("unit_program.mem"),.TIMEOUT_CYCLES(500)) dut(
        .clk(clk),
        .reset_n(reset_n),
        .run_test(run_test),
        .memory_ready(memory_ready),
        .stress_enable(stress_enable),
        .m_axi_awaddr(m_axi_awaddr),
        .m_axi_awlen(m_axi_awlen),
        .m_axi_awsize(m_axi_awsize),
        .m_axi_awburst(m_axi_awburst),
        .m_axi_awlock(m_axi_awlock),
        .m_axi_awcache(m_axi_awcache),
        .m_axi_awprot(m_axi_awprot),
        .m_axi_awqos(m_axi_awqos),
        .m_axi_awvalid(m_axi_awvalid),
        .m_axi_awready(m_axi_awready),
        .m_axi_wdata(m_axi_wdata),
        .m_axi_wstrb(m_axi_wstrb),
        .m_axi_wlast(m_axi_wlast),
        .m_axi_wvalid(m_axi_wvalid),
        .m_axi_wready(m_axi_wready),
        .m_axi_bresp(m_axi_bresp),
        .m_axi_bvalid(m_axi_bvalid),
        .m_axi_bready(m_axi_bready),
        .m_axi_araddr(m_axi_araddr),
        .m_axi_arlen(m_axi_arlen),
        .m_axi_arsize(m_axi_arsize),
        .m_axi_arburst(m_axi_arburst),
        .m_axi_arlock(m_axi_arlock),
        .m_axi_arcache(m_axi_arcache),
        .m_axi_arprot(m_axi_arprot),
        .m_axi_arqos(m_axi_arqos),
        .m_axi_arvalid(m_axi_arvalid),
        .m_axi_arready(m_axi_arready),
        .m_axi_rdata(m_axi_rdata),
        .m_axi_rresp(m_axi_rresp),
        .m_axi_rlast(m_axi_rlast),
        .m_axi_rvalid(m_axi_rvalid),
        .m_axi_rready(m_axi_rready),
        .m_axil_awaddr(m_axil_awaddr),
        .m_axil_awprot(m_axil_awprot),
        .m_axil_awvalid(m_axil_awvalid),
        .m_axil_awready(m_axil_awready),
        .m_axil_wdata(m_axil_wdata),
        .m_axil_wstrb(m_axil_wstrb),
        .m_axil_wvalid(m_axil_wvalid),
        .m_axil_wready(m_axil_wready),
        .m_axil_bresp(m_axil_bresp),
        .m_axil_bvalid(m_axil_bvalid),
        .m_axil_bready(m_axil_bready),
        .m_axil_araddr(m_axil_araddr),
        .m_axil_arprot(m_axil_arprot),
        .m_axil_arvalid(m_axil_arvalid),
        .m_axil_arready(m_axil_arready),
        .m_axil_rdata(m_axil_rdata),
        .m_axil_rresp(m_axil_rresp),
        .m_axil_rvalid(m_axil_rvalid),
        .m_axil_rready(m_axil_rready),
        .test_busy(test_busy),
        .test_done(test_done),
        .test_pass(test_pass),
        .test_fail(test_fail),
        .fail_code(fail_code),
        .cases_done(cases_done),
        .last_case_cycles(last_case_cycles),
        .debug_pc(debug_pc),
        .debug_state(debug_state),
        .actual_word(actual_word));

    reg [63:0] memory [0:31];
    reg [31:0] control_value;
    reg [31:0] aw_address, ar_address;
    reg [63:0] w_data;
    reg aw_pending, w_pending;
    reg axil_aw_pending, axil_w_pending;
    reg [31:0] axil_aw_address, axil_w_data;
    integer tick, polls;
    reg inject, timeout_mode;
    reg held_aw, held_w, held_ar;
    reg [31:0] last_aw, last_ar;
    reg [63:0] last_w;
    initial clk = 0;
    always #5 clk = !clk;
    always @* begin
        m_axi_awready = !timeout_mode && !aw_pending && tick%3==0;
        m_axi_wready = !timeout_mode && !w_pending && tick%5==0;
        m_axi_arready = !timeout_mode && !m_axi_rvalid && tick%7==0;
        m_axil_awready = !axil_aw_pending && tick%5==0;
        m_axil_wready = !axil_w_pending && tick%3==0;
        m_axil_arready = !m_axil_rvalid && tick%4==0;
    end
    always @(posedge clk) begin
        if(!reset_n) begin
            tick <= 0; polls <= 0; aw_pending <= 0; w_pending <= 0;
            axil_aw_pending <= 0; axil_w_pending <= 0;
            m_axi_bvalid <= 0; m_axi_bresp <= 0;
            m_axi_rvalid <= 0; m_axi_rresp <= 0; m_axi_rdata <= 0; m_axi_rlast <= 1;
            m_axil_bvalid <= 0; m_axil_bresp <= 0;
            m_axil_rvalid <= 0; m_axil_rresp <= 0; m_axil_rdata <= 0;
            held_aw <= 0; held_w <= 0; held_ar <= 0;
        end else begin
            tick <= tick+1;
            if(held_aw && (!m_axi_awvalid || m_axi_awaddr!==last_aw)) $fatal(1,"AW payload changed while stalled");
            if(held_w && (!m_axi_wvalid || m_axi_wdata!==last_w)) $fatal(1,"W payload changed while stalled");
            if(held_ar && (!m_axi_arvalid || m_axi_araddr!==last_ar)) $fatal(1,"AR payload changed while stalled");
            held_aw <= m_axi_awvalid && !m_axi_awready; last_aw <= m_axi_awaddr;
            held_w <= m_axi_wvalid && !m_axi_wready; last_w <= m_axi_wdata;
            held_ar <= m_axi_arvalid && !m_axi_arready; last_ar <= m_axi_araddr;
            if(m_axi_awvalid && m_axi_awready) begin
                if(m_axi_awlen!=0 || m_axi_awsize!=3 || m_axi_awburst!=1) $fatal(1,"Unexpected AXI write attributes");
                aw_address <= m_axi_awaddr; aw_pending <= 1;
            end
            if(m_axi_wvalid && m_axi_wready) begin
                if(!m_axi_wlast || m_axi_wstrb!=8'hff) $fatal(1,"Unexpected AXI write mask/last");
                w_data <= m_axi_wdata; w_pending <= 1;
            end
            if(aw_pending && w_pending && !m_axi_bvalid) begin
                memory[aw_address>>3] <= w_data; m_axi_bvalid <= 1;
            end
            if(m_axi_bvalid && m_axi_bready) begin
                m_axi_bvalid <= 0; aw_pending <= 0; w_pending <= 0;
            end
            if(m_axi_arvalid && m_axi_arready) begin
                m_axi_rdata <= memory[m_axi_araddr>>3] ^ (inject ? 64'd1 : 64'd0);
                m_axi_rvalid <= 1;
            end
            if(m_axi_rvalid && m_axi_rready) m_axi_rvalid <= 0;
            if(m_axil_awvalid && m_axil_awready) begin axil_aw_address <= m_axil_awaddr; axil_aw_pending <= 1; end
            if(m_axil_wvalid && m_axil_wready) begin axil_w_data <= m_axil_wdata; axil_w_pending <= 1; end
            if(axil_aw_pending && axil_w_pending && !m_axil_bvalid) begin
                if(axil_aw_address!=32'h40) $fatal(1,"Unexpected control write");
                control_value <= axil_w_data; m_axil_bvalid <= 1;
            end
            if(m_axil_bvalid && m_axil_bready) begin m_axil_bvalid <= 0; axil_aw_pending <= 0; axil_w_pending <= 0; end
            if(m_axil_arvalid && m_axil_arready) begin
                m_axil_rvalid <= 1;
                if(m_axil_araddr==0) begin
                    m_axil_rdata <= polls>=2 ? 2 : 0; polls <= polls+1;
                end else m_axil_rdata <= control_value;
            end
            if(m_axil_rvalid && m_axil_rready) m_axil_rvalid <= 0;
        end
    end
    initial begin
        reset_n = 0; run_test = 0; memory_ready = 1; stress_enable = 1;
        inject = $test$plusargs("inject"); timeout_mode = $test$plusargs("timeout");
        #100; @(negedge clk) reset_n = 1;
        repeat(5) @(posedge clk);
        @(negedge clk) run_test = 1;
        if($test$plusargs("reset")) begin
            wait(debug_state==4);
            @(negedge clk) begin reset_n = 0; run_test = 0; end
            repeat(3) @(posedge clk);
            @(negedge clk) reset_n = 1;
            repeat(5) @(posedge clk);
            @(negedge clk) run_test = 1;
        end
        wait(test_done);
        if(inject) begin
            if(test_pass || !test_fail || fail_code!=8'h02) $fatal(1,"Corruption was not detected");
        end else if(timeout_mode) begin
            if(test_pass || !test_fail || fail_code!=8'hff) $fatal(1,"Timeout was not detected");
            repeat(10) @(posedge clk);
            if(!m_axi_awvalid || !m_axi_wvalid) $fatal(1,"Timeout cancelled pending VALID");
            @(negedge clk) timeout_mode = 0;
            wait(debug_state==0);
            repeat(10) @(posedge clk);
            if(m_axi_awvalid || m_axi_wvalid || test_pass || !test_fail) $fatal(1,"Timeout did not drain safely");
        end else begin
            if(!test_pass || test_fail || cases_done!=26) $fatal(1,"Unit failure code=%h pc=%d",fail_code,debug_pc);
        end
        $display("SELFTEST UNIT PASS inject=%b timeout=%b cases=%d",inject,timeout_mode,cases_done);
        $finish;
    end
    initial begin #200000; $fatal(1,"Unit bench timeout"); end
endmodule
