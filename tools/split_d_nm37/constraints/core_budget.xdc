# Read late, after Clocking Wizard has defined the generated clocks.
# Both HBM references are physically driven by the same board IBUFDS output.
# Replace the IP-scoped primary clocks with divide-by-one generated clocks so
# timing retains the board clock insertion delay and common source relationship.
# XDC accepts constraint/query commands; assertions belong in regular Tcl.
create_generated_clock -name u_bd/split_d_system_i/hbm_0/inst/HBM_REF_CLK_0 \
    -source [get_ports board_clk_p] -divide_by 1 -combinational \
    [get_pins u_bd/split_d_system_i/hbm_0/inst/HBM_REF_CLK_0]
create_generated_clock -name u_bd/split_d_system_i/hbm_0/inst/HBM_REF_CLK_1 \
    -source [get_ports board_clk_p] -divide_by 1 -combinational \
    [get_pins u_bd/split_d_system_i/hbm_0/inst/HBM_REF_CLK_1]
set_clock_uncertainty -setup 2.700 \
    [get_clocks -of_objects [get_pins u_bd/split_d_system_i/fsa_0/ap_clk]]
