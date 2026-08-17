`timescale 1ns / 1ps

// Template for a module-specific NM37 board top.
// Replace every @...@ placeholder and adapt the instance port maps to the
// actual exported HLS RTL. Do not assume a single input_r/output_r interface.
module @BOARD_TOP@ (
    input wire board_clk_p,
    input wire board_clk_n,
    input wire reset_n
);

    wire clk_100m;
    wire clk_locked;

    @CLOCK_WIZARD_NAME@ u_clk_wiz (
        .clk_in1_p (board_clk_p),
        .clk_in1_n (board_clk_n),
        .reset     (~reset_n),
        .clk_out1  (clk_100m),
        .locked    (clk_locked)
    );

    // Assert from the external reset directly; release synchronously after
    // Clocking Wizard has locked. locked is sampled synchronously here and is
    // not combined through a LUT that drives asynchronous reset pins.
    (* ASYNC_REG = "TRUE" *) reg [7:0] reset_shift = 8'b0;
    always @(posedge clk_100m or negedge reset_n) begin
        if (!reset_n)
            reset_shift <= 8'b0;
        else if (!clk_locked)
            reset_shift <= 8'b0;
        else
            reset_shift <= {reset_shift[6:0], 1'b1};
    end

    wire rst_100m = ~reset_shift[7];

    @MODULE_SIGNAL_DECLARATIONS@

    @TEST_CONTROLLER_INSTANCE@

    @HLS_IP_INSTANCE@

    @VIO_INSTANCE@

    @ILA_INSTANCE@

endmodule

