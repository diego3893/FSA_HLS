"""Generate protocol-unit fixture only; this is not a replacement HBM test."""
from pathlib import Path
import re

OUT = Path(__file__).resolve().parent
rtl = (OUT.parent / "rtl/split_d_selftest.sv").read_text()
ports = re.findall(r"^\s*(input|output)\s+(wire|reg)\s+(\[[^]]+\]\s+)?(\w+)\s*[,)]?\s*$", rtl, re.M)
decls, connections = [], []
for direction, kind, width, name in ports:
    decls.append(f"    {'reg' if direction=='input' else 'wire'} {width}{name};")
    connections.append(f".{name}({name})")


def word(op, addr=0, data=0, count=1):
    return (op << 124) | (addr << 92) | (data << 28) | (count << 8)


commands = [word(1, 0, 0x0123456789abcdef), word(6, 16, 0xaabbccdd, 5),
            word(3, 0, 0x0123456789abcdef), word(7, 16, 0xaabbccdd, 5),
            word(2, 0x40, 9), word(4, 0x40, (0xffffffff << 32) | 9),
            word(5, 0, (2 << 32) | 2)] + [word(8)] * 26 + [word(15)]
(OUT / "unit_program.mem").write_text("".join(f"{v:032x}\n" for v in commands + [word(15)] * (8192-len(commands))), encoding="ascii", newline="\n")
body = r'''
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
'''
text = "`timescale 1ns/1ps\nmodule tb_split_d_selftest;\n" + "\n".join(decls)
text += '\n    split_d_selftest #(.ROM_FILE("unit_program.mem"),.TIMEOUT_CYCLES(500)) dut(\n        '
text += ",\n        ".join(connections) + ");\n" + body + "endmodule\n"
(OUT / "tb_split_d_selftest.sv").write_text(text, encoding="utf-8", newline="\n")
print(f"Generated protocol-unit fixture with {len(ports)} checked port connections")
