`timescale 1ns/1ps
// Passive simulation checker. It observes the actual FSA-to-SmartConnect
// boundary; it neither supplies memory data nor changes READY/VALID.
module axi_watch #(
    parameter NAME = "axi"
)(
    input wire clk, reset_n,
    input wire awvalid, awready, wvalid, wready, bvalid, bready,
    input wire arvalid, arready, rvalid, rready,
    input wire [127:0] awpayload, wpayload, arpayload, rpayload,
    input wire [7:0] bpayload,
    input wire [1:0] bresp, rresp
);
    reg [4:0] held;
    reg [127:0] previous_aw, previous_w, previous_ar, previous_r;
    reg [7:0] previous_b;
    longint unsigned aw_count=0, ar_count=0, w_count=0, r_count=0;
    longint unsigned aw_stalls=0, ar_stalls=0, w_stalls=0, r_stalls=0, b_stalls=0;
    always @(posedge clk) begin
        if(!reset_n) begin
            held <= 0;
            aw_count <= 0; ar_count <= 0; w_count <= 0; r_count <= 0;
            aw_stalls <= 0; ar_stalls <= 0; w_stalls <= 0; r_stalls <= 0; b_stalls <= 0;
        end else begin
            if(held[0] && (!awvalid || awpayload!==previous_aw)) $fatal(1,"%s AW changed while stalled",NAME);
            if(held[1] && (!wvalid || wpayload!==previous_w)) $fatal(1,"%s W changed while stalled",NAME);
            if(held[2] && (!bvalid || bpayload!==previous_b)) $fatal(1,"%s B changed while stalled",NAME);
            if(held[3] && (!arvalid || arpayload!==previous_ar)) $fatal(1,"%s AR changed while stalled",NAME);
            if(held[4] && (!rvalid || rpayload!==previous_r)) $fatal(1,"%s R changed while stalled",NAME);
            held <= {rvalid && !rready,arvalid && !arready,bvalid && !bready,wvalid && !wready,awvalid && !awready};
            previous_aw <= awpayload; previous_w <= wpayload; previous_b <= bpayload;
            previous_ar <= arpayload; previous_r <= rpayload;
            if(awvalid && awready) aw_count <= aw_count+1;
            if(arvalid && arready) ar_count <= ar_count+1;
            if(wvalid && wready) w_count <= w_count+1;
            if(rvalid && rready) r_count <= r_count+1;
            if(awvalid && !awready) aw_stalls <= aw_stalls+1;
            if(arvalid && !arready) ar_stalls <= ar_stalls+1;
            if(wvalid && !wready) w_stalls <= w_stalls+1;
            if(rvalid && !rready) r_stalls <= r_stalls+1;
            if(bvalid && !bready) b_stalls <= b_stalls+1;
            if(bvalid && bready && bresp!=0) $fatal(1,"%s BRESP error",NAME);
            if(rvalid && rready && rresp!=0) $fatal(1,"%s RRESP error",NAME);
        end
    end
    final $display("AXI OBSERVED %s AW=%0d AR=%0d W=%0d R=%0d stalls AW=%0d AR=%0d W=%0d R=%0d B=%0d",
        NAME,aw_count,ar_count,w_count,r_count,aw_stalls,ar_stalls,w_stalls,r_stalls,b_stalls);
endmodule
