`timescale 1ns/1ps
// Observe the core FSM only; excludes preload and output verification.
module core_latency_watch(input wire clk, reset_n, start, idle, done);
    longint unsigned tick=0, begin_tick=0;
    integer transaction=0;
    reg active=0;
    always @(posedge clk) begin
        if(!reset_n) begin tick <= 0; active <= 0; transaction <= 0; end
        else begin
            tick <= tick+1;
            if(start && idle && !active) begin begin_tick <= tick; active <= 1; end
            if(done && active) begin
                $display("CORE LATENCY transaction=%0d cycles=%0d",transaction,tick-begin_tick);
                transaction <= transaction+1; active <= 0;
            end
        end
    end
endmodule
