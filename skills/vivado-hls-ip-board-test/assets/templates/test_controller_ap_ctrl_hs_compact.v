`timescale 1ns / 1ps

// Template only for an ap_ctrl_hs IP with one packed input and one packed
// output. Generate a different controller for scalar, AXI or AXIS interfaces.
module @TEST_CONTROLLER@ (
    input  wire                          clk,
    input  wire                          rst,
    input  wire                          run_test,

    output reg                           ap_start,
    input  wire                          ap_ready,
    input  wire                          ap_done,
    input  wire                          ap_idle,

    output reg  [@INPUT_WIDTH_MINUS_1@:0] input_r,
    input  wire [@OUTPUT_WIDTH_MINUS_1@:0] output_r,

    output reg                           test_busy,
    output reg                           test_done,
    output reg                           test_pass,
    output reg                           test_fail,
    output wire [@TX_WIDTH_MINUS_1@:0]    debug_transaction,
    output wire [2:0]                    debug_state
);

    localparam [@TX_WIDTH_MINUS_1@:0] LAST_TRANSACTION = @LAST_TRANSACTION@;

    localparam [2:0] ST_IDLE        = 3'd0;
    localparam [2:0] ST_PREPARE     = 3'd1;
    localparam [2:0] ST_WAIT_ACCEPT = 3'd2;
    localparam [2:0] ST_WAIT_DONE   = 3'd3;
    localparam [2:0] ST_FINISHED    = 3'd4;

    reg [2:0] state;
    reg [@TX_WIDTH_MINUS_1@:0] transaction_index;
    reg run_test_d;
    reg output_check_error;

    wire run_test_rise = run_test & ~run_test_d;
    assign debug_transaction = transaction_index;
    assign debug_state = state;

    function [@INPUT_WIDTH_MINUS_1@:0] build_transaction;
        input [@TX_WIDTH_MINUS_1@:0] transaction;
        reg [@INPUT_WIDTH_MINUS_1@:0] word_value;
        begin
            word_value = {@INPUT_WIDTH@{1'b0}};
            @BUILD_TRANSACTION_BODY@
            build_transaction = word_value;
        end
    endfunction

    // Compare output_r with an independently derived golden result.
    always @(*) begin
        output_check_error = 1'b0;
        @CHECK_OUTPUT_BODY@
    end

    always @(posedge clk) begin
        if (rst) begin
            state             <= ST_IDLE;
            transaction_index <= {@TX_WIDTH@{1'b0}};
            run_test_d        <= 1'b0;
            ap_start          <= 1'b0;
            input_r           <= {@INPUT_WIDTH@{1'b0}};
            test_busy         <= 1'b0;
            test_done         <= 1'b0;
            test_pass         <= 1'b0;
            test_fail         <= 1'b0;
        end else begin
            run_test_d <= run_test;

            // ap_done may coincide with ap_ready for a short transaction.
            if ((state == ST_WAIT_ACCEPT || state == ST_WAIT_DONE) && ap_done) begin
                ap_start  <= 1'b0;
                test_fail <= test_fail | output_check_error;

                if (transaction_index == LAST_TRANSACTION) begin
                    state     <= ST_FINISHED;
                    test_busy <= 1'b0;
                    test_done <= 1'b1;
                    test_pass <= ~(test_fail | output_check_error);
                end else begin
                    transaction_index <= transaction_index + 1'b1;
                    state <= ST_PREPARE;
                end
            end else begin
                case (state)
                    ST_IDLE: begin
                        ap_start <= 1'b0;
                        if (run_test_rise) begin
                            transaction_index <= {@TX_WIDTH@{1'b0}};
                            test_busy <= 1'b1;
                            test_done <= 1'b0;
                            test_pass <= 1'b0;
                            test_fail <= 1'b0;
                            state <= ST_PREPARE;
                        end
                    end

                    ST_PREPARE: begin
                        input_r  <= build_transaction(transaction_index);
                        ap_start <= 1'b1;
                        state    <= ST_WAIT_ACCEPT;
                    end

                    ST_WAIT_ACCEPT: begin
                        if (ap_ready) begin
                            ap_start <= 1'b0;
                            state <= ST_WAIT_DONE;
                        end
                    end

                    ST_WAIT_DONE: ap_start <= 1'b0;

                    ST_FINISHED: begin
                        ap_start <= 1'b0;
                        if (run_test_rise) begin
                            transaction_index <= {@TX_WIDTH@{1'b0}};
                            test_busy <= 1'b1;
                            test_done <= 1'b0;
                            test_pass <= 1'b0;
                            test_fail <= 1'b0;
                            state <= ST_PREPARE;
                        end
                    end

                    default: state <= ST_IDLE;
                endcase
            end
        end
    end

    wire unused_ap_idle = ap_idle;

endmodule

