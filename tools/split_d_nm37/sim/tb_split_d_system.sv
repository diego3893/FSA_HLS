`timescale 1ns/1ps
// Exercise the same generated HBM/SmartConnect/FSA/selftest BD as hardware.
// The simulation bypasses only package IBUFDS and JTAG VIO/ILA.
module tb_split_d_system;
    reg clk = 0;
    always #5 clk = !clk;
    reg reset_n = 0, run_test = 0, stress_enable = 0;
    wire ctrl_clk, clock_locked, init_done, busy, done, pass, fail;
    wire [7:0] code;
    wire [5:0] cases;
    wire [12:0] pc;
    wire [3:0] state;
    wire [31:0] last_cycles;
    wire [63:0] actual;
    reg [5:0] previous_cases = 0;
    always @(posedge ctrl_clk) begin
        if(!reset_n) previous_cases <= 0;
        else if(cases!=previous_cases) begin
            if(cases!=0) $display("SYSTEM PROGRESS mode=%b completed=%0d full_case_cycles=%0d",stress_enable,cases,last_cycles);
            previous_cases <= cases;
        end
    end
    split_d_system_wrapper dut(
        .sys_clk_100(clk),.reset_n(reset_n),.run_test(run_test),.stress_enable(stress_enable),
        .ctrl_clk(ctrl_clk),.clock_locked(clock_locked),.init_done(init_done),
        .test_busy(busy),.test_done(done),.test_pass(pass),.test_fail(fail),
        .fail_code(code),.cases_done(cases),.debug_pc(pc),.debug_state(state),
        .last_case_cycles(last_cycles),.actual_word(actual)
    );
`ifdef NM37_HBM_TLM_FUNCTIONAL
    // The official hbm_sc.h declares apb_complete as xsc_stub_port. TLM
    // does not model calibration; enable functional traffic after lock.
    // RTL/default runs never enable this adapter and must await real init.
    initial begin
        $display("HBM TLM FUNCTIONAL ONLY: APB initialization is unmodeled; readiness adapter enabled");
        force dut.split_d_system_i.init_done = clock_locked;
    end
`endif
    initial begin
        #1000 reset_n = 1;
        wait(clock_locked && init_done);
        repeat(20) @(posedge ctrl_clk);
`ifndef NM37_HBM_TLM_FUNCTIONAL
        // APB reset reinitializes the complete HBM only in the RTL model.
        // TLM stubs APB and cannot validate reset after memory traffic.
        // Reset the entire clock/reset/AXI/HBM system during an active write.
        @(negedge ctrl_clk) run_test = 1;
        wait(state==5);
        @(negedge ctrl_clk) begin reset_n = 0; run_test = 0; end
        #1000 reset_n = 1;
        wait(clock_locked && init_done);
        repeat(20) @(posedge ctrl_clk);
        $display("SYSTEM RESET EXERCISE initialization restored");
`else
        $display("HBM TLM FUNCTIONAL ONLY: busy-reset recovery excluded; mandatory in RTL and board validation");
`endif
        @(negedge ctrl_clk) run_test = 1;
        wait(done);
        if(!pass || fail || cases!=26) $fatal(1,"SYSTEM FAIL mode0 cases=%0d code=%h pc=%0d actual=%h",cases,code,pc,actual);
        $display("SYSTEM PASS mode0 cases=%0d",cases);
        @(negedge ctrl_clk) run_test = 0;
        repeat(20) @(posedge ctrl_clk);
        @(negedge ctrl_clk) begin stress_enable = 1; run_test = 1; end
        wait(busy);
        wait(done);
        if(!pass || fail || cases!=26) $fatal(1,"SYSTEM FAIL mode1 cases=%0d code=%h pc=%0d actual=%h",cases,code,pc,actual);
        $display("SYSTEM PASS mode1 cases=%0d",cases);
        $finish;
    end
    initial begin
        #10000;
        if(clock_locked!==1'b1 || dut.split_d_system_i.selftest_0.reset_n!==1'b1 ||
            dut.split_d_system_i.hbm_0.AXI_00_ARESET_N!==1'b1 ||
            dut.split_d_system_i.hbm_0.APB_0_PRESET_N!==1'b1 ||
            dut.split_d_system_i.hbm_0.APB_1_PRESET_N!==1'b1)
            $fatal(1,"SYSTEM RESET RELEASE FAIL at 10us");
        $display("SYSTEM CLOCK DIAGNOSTIC time=%t ctrl_clk=%b locked=%b init_done=%b reset_n=%b",$time,ctrl_clk,clock_locked,init_done,reset_n);
    end
    initial begin
        forever begin
            #50000;
            $display("SYSTEM HEARTBEAT time=%t locked=%b apb0=%b apb1=%b reset100=%b state=%0d pc=%0d cases=%0d",
                $time,clock_locked,dut.split_d_system_i.hbm_0.apb_complete_0,
                dut.split_d_system_i.hbm_0.apb_complete_1,
                dut.split_d_system_i.selftest_0.reset_n,state,pc,cases);
        end
    end
    initial begin
        #(64'd3000000000);
        $fatal(1,"SYSTEM TIMEOUT busy=%b done=%b cases=%0d pc=%0d state=%0d",busy,done,cases,pc,state);
    end
    // Observe the final SmartConnect-to-HBM boundary. Width conversion
    // alone does not guarantee native HBM SIZE=5 for single-beat accesses.
    always @(posedge dut.split_d_system_i.hbm_0.AXI_00_ACLK) begin
        if(dut.split_d_system_i.hbm_0.AXI_00_ARESET_N) begin
            if(dut.split_d_system_i.memory_interconnect_M00_AXI_AWVALID && dut.split_d_system_i.memory_interconnect_M00_AXI_AWREADY) begin
                if(dut.split_d_system_i.memory_interconnect_M00_AXI_AWSIZE!==3'd5 || dut.split_d_system_i.memory_interconnect_M00_AXI_AWADDR[4:0]!==5'b0)
                    $fatal(1,"HBM native write SIZE/alignment violation");
            end
            if(dut.split_d_system_i.memory_interconnect_M00_AXI_ARVALID && dut.split_d_system_i.memory_interconnect_M00_AXI_ARREADY) begin
                if(dut.split_d_system_i.memory_interconnect_M00_AXI_ARSIZE!==3'd5 || dut.split_d_system_i.memory_interconnect_M00_AXI_ARADDR[4:0]!==5'b0)
                    $fatal(1,"HBM native read SIZE/alignment violation");
            end
        end
    end
    // Explicit passive instances: Vivado omits standalone bind files from
    // automatic simulation compile order. No memory responses are driven.
    axi_watch #(.NAME("q")) watch_q(
        .clk(dut.split_d_system_i.fsa_0.inst.ap_clk),
        .reset_n(dut.split_d_system_i.fsa_0.inst.ap_rst_n),
        .awvalid(dut.split_d_system_i.fsa_0.inst.m_axi_q_gmem_AWVALID),
        .awready(dut.split_d_system_i.fsa_0.inst.m_axi_q_gmem_AWREADY),
        .wvalid(dut.split_d_system_i.fsa_0.inst.m_axi_q_gmem_WVALID),
        .wready(dut.split_d_system_i.fsa_0.inst.m_axi_q_gmem_WREADY),
        .bvalid(dut.split_d_system_i.fsa_0.inst.m_axi_q_gmem_BVALID),
        .bready(dut.split_d_system_i.fsa_0.inst.m_axi_q_gmem_BREADY),
        .arvalid(dut.split_d_system_i.fsa_0.inst.m_axi_q_gmem_ARVALID),
        .arready(dut.split_d_system_i.fsa_0.inst.m_axi_q_gmem_ARREADY),
        .rvalid(dut.split_d_system_i.fsa_0.inst.m_axi_q_gmem_RVALID),
        .rready(dut.split_d_system_i.fsa_0.inst.m_axi_q_gmem_RREADY),
        .bresp(dut.split_d_system_i.fsa_0.inst.m_axi_q_gmem_BRESP),
        .rresp(dut.split_d_system_i.fsa_0.inst.m_axi_q_gmem_RRESP),
        .awpayload({dut.split_d_system_i.fsa_0.inst.m_axi_q_gmem_AWADDR,dut.split_d_system_i.fsa_0.inst.m_axi_q_gmem_AWID,dut.split_d_system_i.fsa_0.inst.m_axi_q_gmem_AWLEN,dut.split_d_system_i.fsa_0.inst.m_axi_q_gmem_AWSIZE,dut.split_d_system_i.fsa_0.inst.m_axi_q_gmem_AWBURST,dut.split_d_system_i.fsa_0.inst.m_axi_q_gmem_AWLOCK,dut.split_d_system_i.fsa_0.inst.m_axi_q_gmem_AWCACHE,dut.split_d_system_i.fsa_0.inst.m_axi_q_gmem_AWPROT,dut.split_d_system_i.fsa_0.inst.m_axi_q_gmem_AWQOS,dut.split_d_system_i.fsa_0.inst.m_axi_q_gmem_AWREGION,dut.split_d_system_i.fsa_0.inst.m_axi_q_gmem_AWUSER}),
        .arpayload({dut.split_d_system_i.fsa_0.inst.m_axi_q_gmem_ARADDR,dut.split_d_system_i.fsa_0.inst.m_axi_q_gmem_ARID,dut.split_d_system_i.fsa_0.inst.m_axi_q_gmem_ARLEN,dut.split_d_system_i.fsa_0.inst.m_axi_q_gmem_ARSIZE,dut.split_d_system_i.fsa_0.inst.m_axi_q_gmem_ARBURST,dut.split_d_system_i.fsa_0.inst.m_axi_q_gmem_ARLOCK,dut.split_d_system_i.fsa_0.inst.m_axi_q_gmem_ARCACHE,dut.split_d_system_i.fsa_0.inst.m_axi_q_gmem_ARPROT,dut.split_d_system_i.fsa_0.inst.m_axi_q_gmem_ARQOS,dut.split_d_system_i.fsa_0.inst.m_axi_q_gmem_ARREGION,dut.split_d_system_i.fsa_0.inst.m_axi_q_gmem_ARUSER}),
        .wpayload({dut.split_d_system_i.fsa_0.inst.m_axi_q_gmem_WDATA,dut.split_d_system_i.fsa_0.inst.m_axi_q_gmem_WSTRB,dut.split_d_system_i.fsa_0.inst.m_axi_q_gmem_WLAST,dut.split_d_system_i.fsa_0.inst.m_axi_q_gmem_WID,dut.split_d_system_i.fsa_0.inst.m_axi_q_gmem_WUSER}),
        .rpayload({dut.split_d_system_i.fsa_0.inst.m_axi_q_gmem_RDATA,dut.split_d_system_i.fsa_0.inst.m_axi_q_gmem_RRESP,dut.split_d_system_i.fsa_0.inst.m_axi_q_gmem_RLAST,dut.split_d_system_i.fsa_0.inst.m_axi_q_gmem_RID,dut.split_d_system_i.fsa_0.inst.m_axi_q_gmem_RUSER}),
        .bpayload({dut.split_d_system_i.fsa_0.inst.m_axi_q_gmem_BRESP,dut.split_d_system_i.fsa_0.inst.m_axi_q_gmem_BID,dut.split_d_system_i.fsa_0.inst.m_axi_q_gmem_BUSER})
    );
    axi_watch #(.NAME("k")) watch_k(
        .clk(dut.split_d_system_i.fsa_0.inst.ap_clk),
        .reset_n(dut.split_d_system_i.fsa_0.inst.ap_rst_n),
        .awvalid(dut.split_d_system_i.fsa_0.inst.m_axi_k_gmem_AWVALID),
        .awready(dut.split_d_system_i.fsa_0.inst.m_axi_k_gmem_AWREADY),
        .wvalid(dut.split_d_system_i.fsa_0.inst.m_axi_k_gmem_WVALID),
        .wready(dut.split_d_system_i.fsa_0.inst.m_axi_k_gmem_WREADY),
        .bvalid(dut.split_d_system_i.fsa_0.inst.m_axi_k_gmem_BVALID),
        .bready(dut.split_d_system_i.fsa_0.inst.m_axi_k_gmem_BREADY),
        .arvalid(dut.split_d_system_i.fsa_0.inst.m_axi_k_gmem_ARVALID),
        .arready(dut.split_d_system_i.fsa_0.inst.m_axi_k_gmem_ARREADY),
        .rvalid(dut.split_d_system_i.fsa_0.inst.m_axi_k_gmem_RVALID),
        .rready(dut.split_d_system_i.fsa_0.inst.m_axi_k_gmem_RREADY),
        .bresp(dut.split_d_system_i.fsa_0.inst.m_axi_k_gmem_BRESP),
        .rresp(dut.split_d_system_i.fsa_0.inst.m_axi_k_gmem_RRESP),
        .awpayload({dut.split_d_system_i.fsa_0.inst.m_axi_k_gmem_AWADDR,dut.split_d_system_i.fsa_0.inst.m_axi_k_gmem_AWID,dut.split_d_system_i.fsa_0.inst.m_axi_k_gmem_AWLEN,dut.split_d_system_i.fsa_0.inst.m_axi_k_gmem_AWSIZE,dut.split_d_system_i.fsa_0.inst.m_axi_k_gmem_AWBURST,dut.split_d_system_i.fsa_0.inst.m_axi_k_gmem_AWLOCK,dut.split_d_system_i.fsa_0.inst.m_axi_k_gmem_AWCACHE,dut.split_d_system_i.fsa_0.inst.m_axi_k_gmem_AWPROT,dut.split_d_system_i.fsa_0.inst.m_axi_k_gmem_AWQOS,dut.split_d_system_i.fsa_0.inst.m_axi_k_gmem_AWREGION,dut.split_d_system_i.fsa_0.inst.m_axi_k_gmem_AWUSER}),
        .arpayload({dut.split_d_system_i.fsa_0.inst.m_axi_k_gmem_ARADDR,dut.split_d_system_i.fsa_0.inst.m_axi_k_gmem_ARID,dut.split_d_system_i.fsa_0.inst.m_axi_k_gmem_ARLEN,dut.split_d_system_i.fsa_0.inst.m_axi_k_gmem_ARSIZE,dut.split_d_system_i.fsa_0.inst.m_axi_k_gmem_ARBURST,dut.split_d_system_i.fsa_0.inst.m_axi_k_gmem_ARLOCK,dut.split_d_system_i.fsa_0.inst.m_axi_k_gmem_ARCACHE,dut.split_d_system_i.fsa_0.inst.m_axi_k_gmem_ARPROT,dut.split_d_system_i.fsa_0.inst.m_axi_k_gmem_ARQOS,dut.split_d_system_i.fsa_0.inst.m_axi_k_gmem_ARREGION,dut.split_d_system_i.fsa_0.inst.m_axi_k_gmem_ARUSER}),
        .wpayload({dut.split_d_system_i.fsa_0.inst.m_axi_k_gmem_WDATA,dut.split_d_system_i.fsa_0.inst.m_axi_k_gmem_WSTRB,dut.split_d_system_i.fsa_0.inst.m_axi_k_gmem_WLAST,dut.split_d_system_i.fsa_0.inst.m_axi_k_gmem_WID,dut.split_d_system_i.fsa_0.inst.m_axi_k_gmem_WUSER}),
        .rpayload({dut.split_d_system_i.fsa_0.inst.m_axi_k_gmem_RDATA,dut.split_d_system_i.fsa_0.inst.m_axi_k_gmem_RRESP,dut.split_d_system_i.fsa_0.inst.m_axi_k_gmem_RLAST,dut.split_d_system_i.fsa_0.inst.m_axi_k_gmem_RID,dut.split_d_system_i.fsa_0.inst.m_axi_k_gmem_RUSER}),
        .bpayload({dut.split_d_system_i.fsa_0.inst.m_axi_k_gmem_BRESP,dut.split_d_system_i.fsa_0.inst.m_axi_k_gmem_BID,dut.split_d_system_i.fsa_0.inst.m_axi_k_gmem_BUSER})
    );
    axi_watch #(.NAME("v")) watch_v(
        .clk(dut.split_d_system_i.fsa_0.inst.ap_clk),
        .reset_n(dut.split_d_system_i.fsa_0.inst.ap_rst_n),
        .awvalid(dut.split_d_system_i.fsa_0.inst.m_axi_v_gmem_AWVALID),
        .awready(dut.split_d_system_i.fsa_0.inst.m_axi_v_gmem_AWREADY),
        .wvalid(dut.split_d_system_i.fsa_0.inst.m_axi_v_gmem_WVALID),
        .wready(dut.split_d_system_i.fsa_0.inst.m_axi_v_gmem_WREADY),
        .bvalid(dut.split_d_system_i.fsa_0.inst.m_axi_v_gmem_BVALID),
        .bready(dut.split_d_system_i.fsa_0.inst.m_axi_v_gmem_BREADY),
        .arvalid(dut.split_d_system_i.fsa_0.inst.m_axi_v_gmem_ARVALID),
        .arready(dut.split_d_system_i.fsa_0.inst.m_axi_v_gmem_ARREADY),
        .rvalid(dut.split_d_system_i.fsa_0.inst.m_axi_v_gmem_RVALID),
        .rready(dut.split_d_system_i.fsa_0.inst.m_axi_v_gmem_RREADY),
        .bresp(dut.split_d_system_i.fsa_0.inst.m_axi_v_gmem_BRESP),
        .rresp(dut.split_d_system_i.fsa_0.inst.m_axi_v_gmem_RRESP),
        .awpayload({dut.split_d_system_i.fsa_0.inst.m_axi_v_gmem_AWADDR,dut.split_d_system_i.fsa_0.inst.m_axi_v_gmem_AWID,dut.split_d_system_i.fsa_0.inst.m_axi_v_gmem_AWLEN,dut.split_d_system_i.fsa_0.inst.m_axi_v_gmem_AWSIZE,dut.split_d_system_i.fsa_0.inst.m_axi_v_gmem_AWBURST,dut.split_d_system_i.fsa_0.inst.m_axi_v_gmem_AWLOCK,dut.split_d_system_i.fsa_0.inst.m_axi_v_gmem_AWCACHE,dut.split_d_system_i.fsa_0.inst.m_axi_v_gmem_AWPROT,dut.split_d_system_i.fsa_0.inst.m_axi_v_gmem_AWQOS,dut.split_d_system_i.fsa_0.inst.m_axi_v_gmem_AWREGION,dut.split_d_system_i.fsa_0.inst.m_axi_v_gmem_AWUSER}),
        .arpayload({dut.split_d_system_i.fsa_0.inst.m_axi_v_gmem_ARADDR,dut.split_d_system_i.fsa_0.inst.m_axi_v_gmem_ARID,dut.split_d_system_i.fsa_0.inst.m_axi_v_gmem_ARLEN,dut.split_d_system_i.fsa_0.inst.m_axi_v_gmem_ARSIZE,dut.split_d_system_i.fsa_0.inst.m_axi_v_gmem_ARBURST,dut.split_d_system_i.fsa_0.inst.m_axi_v_gmem_ARLOCK,dut.split_d_system_i.fsa_0.inst.m_axi_v_gmem_ARCACHE,dut.split_d_system_i.fsa_0.inst.m_axi_v_gmem_ARPROT,dut.split_d_system_i.fsa_0.inst.m_axi_v_gmem_ARQOS,dut.split_d_system_i.fsa_0.inst.m_axi_v_gmem_ARREGION,dut.split_d_system_i.fsa_0.inst.m_axi_v_gmem_ARUSER}),
        .wpayload({dut.split_d_system_i.fsa_0.inst.m_axi_v_gmem_WDATA,dut.split_d_system_i.fsa_0.inst.m_axi_v_gmem_WSTRB,dut.split_d_system_i.fsa_0.inst.m_axi_v_gmem_WLAST,dut.split_d_system_i.fsa_0.inst.m_axi_v_gmem_WID,dut.split_d_system_i.fsa_0.inst.m_axi_v_gmem_WUSER}),
        .rpayload({dut.split_d_system_i.fsa_0.inst.m_axi_v_gmem_RDATA,dut.split_d_system_i.fsa_0.inst.m_axi_v_gmem_RRESP,dut.split_d_system_i.fsa_0.inst.m_axi_v_gmem_RLAST,dut.split_d_system_i.fsa_0.inst.m_axi_v_gmem_RID,dut.split_d_system_i.fsa_0.inst.m_axi_v_gmem_RUSER}),
        .bpayload({dut.split_d_system_i.fsa_0.inst.m_axi_v_gmem_BRESP,dut.split_d_system_i.fsa_0.inst.m_axi_v_gmem_BID,dut.split_d_system_i.fsa_0.inst.m_axi_v_gmem_BUSER})
    );
    axi_watch #(.NAME("o")) watch_o(
        .clk(dut.split_d_system_i.fsa_0.inst.ap_clk),
        .reset_n(dut.split_d_system_i.fsa_0.inst.ap_rst_n),
        .awvalid(dut.split_d_system_i.fsa_0.inst.m_axi_o_gmem_AWVALID),
        .awready(dut.split_d_system_i.fsa_0.inst.m_axi_o_gmem_AWREADY),
        .wvalid(dut.split_d_system_i.fsa_0.inst.m_axi_o_gmem_WVALID),
        .wready(dut.split_d_system_i.fsa_0.inst.m_axi_o_gmem_WREADY),
        .bvalid(dut.split_d_system_i.fsa_0.inst.m_axi_o_gmem_BVALID),
        .bready(dut.split_d_system_i.fsa_0.inst.m_axi_o_gmem_BREADY),
        .arvalid(dut.split_d_system_i.fsa_0.inst.m_axi_o_gmem_ARVALID),
        .arready(dut.split_d_system_i.fsa_0.inst.m_axi_o_gmem_ARREADY),
        .rvalid(dut.split_d_system_i.fsa_0.inst.m_axi_o_gmem_RVALID),
        .rready(dut.split_d_system_i.fsa_0.inst.m_axi_o_gmem_RREADY),
        .bresp(dut.split_d_system_i.fsa_0.inst.m_axi_o_gmem_BRESP),
        .rresp(dut.split_d_system_i.fsa_0.inst.m_axi_o_gmem_RRESP),
        .awpayload({dut.split_d_system_i.fsa_0.inst.m_axi_o_gmem_AWADDR,dut.split_d_system_i.fsa_0.inst.m_axi_o_gmem_AWID,dut.split_d_system_i.fsa_0.inst.m_axi_o_gmem_AWLEN,dut.split_d_system_i.fsa_0.inst.m_axi_o_gmem_AWSIZE,dut.split_d_system_i.fsa_0.inst.m_axi_o_gmem_AWBURST,dut.split_d_system_i.fsa_0.inst.m_axi_o_gmem_AWLOCK,dut.split_d_system_i.fsa_0.inst.m_axi_o_gmem_AWCACHE,dut.split_d_system_i.fsa_0.inst.m_axi_o_gmem_AWPROT,dut.split_d_system_i.fsa_0.inst.m_axi_o_gmem_AWQOS,dut.split_d_system_i.fsa_0.inst.m_axi_o_gmem_AWREGION,dut.split_d_system_i.fsa_0.inst.m_axi_o_gmem_AWUSER}),
        .arpayload({dut.split_d_system_i.fsa_0.inst.m_axi_o_gmem_ARADDR,dut.split_d_system_i.fsa_0.inst.m_axi_o_gmem_ARID,dut.split_d_system_i.fsa_0.inst.m_axi_o_gmem_ARLEN,dut.split_d_system_i.fsa_0.inst.m_axi_o_gmem_ARSIZE,dut.split_d_system_i.fsa_0.inst.m_axi_o_gmem_ARBURST,dut.split_d_system_i.fsa_0.inst.m_axi_o_gmem_ARLOCK,dut.split_d_system_i.fsa_0.inst.m_axi_o_gmem_ARCACHE,dut.split_d_system_i.fsa_0.inst.m_axi_o_gmem_ARPROT,dut.split_d_system_i.fsa_0.inst.m_axi_o_gmem_ARQOS,dut.split_d_system_i.fsa_0.inst.m_axi_o_gmem_ARREGION,dut.split_d_system_i.fsa_0.inst.m_axi_o_gmem_ARUSER}),
        .wpayload({dut.split_d_system_i.fsa_0.inst.m_axi_o_gmem_WDATA,dut.split_d_system_i.fsa_0.inst.m_axi_o_gmem_WSTRB,dut.split_d_system_i.fsa_0.inst.m_axi_o_gmem_WLAST,dut.split_d_system_i.fsa_0.inst.m_axi_o_gmem_WID,dut.split_d_system_i.fsa_0.inst.m_axi_o_gmem_WUSER}),
        .rpayload({dut.split_d_system_i.fsa_0.inst.m_axi_o_gmem_RDATA,dut.split_d_system_i.fsa_0.inst.m_axi_o_gmem_RRESP,dut.split_d_system_i.fsa_0.inst.m_axi_o_gmem_RLAST,dut.split_d_system_i.fsa_0.inst.m_axi_o_gmem_RID,dut.split_d_system_i.fsa_0.inst.m_axi_o_gmem_RUSER}),
        .bpayload({dut.split_d_system_i.fsa_0.inst.m_axi_o_gmem_BRESP,dut.split_d_system_i.fsa_0.inst.m_axi_o_gmem_BID,dut.split_d_system_i.fsa_0.inst.m_axi_o_gmem_BUSER})
    );
    core_latency_watch latency_watch(
        .clk(ctrl_clk),.reset_n(dut.split_d_system_i.fsa_0.inst.ap_rst_n),
        .start(dut.split_d_system_i.fsa_0.inst.ap_start),.idle(dut.split_d_system_i.fsa_0.inst.ap_idle),.done(dut.split_d_system_i.fsa_0.inst.ap_done)
    );
endmodule
