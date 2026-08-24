## NM37 onboard 100 MHz differential clock: CLK_100_DDR_P/N.
set_property PACKAGE_PIN BH42 [get_ports board_clk_p]
set_property PACKAGE_PIN BJ42 [get_ports board_clk_n]
set_property IOSTANDARD DIFF_SSTL12 [get_ports {board_clk_p board_clk_n}]

## NM37 onboard SW2: SLOT_CPU_RESET, active low.
set_property PACKAGE_PIN BF2 [get_ports reset_n]
set_property IOSTANDARD LVCMOS18 [get_ports reset_n]

## reset_n is an asynchronous board control, not synchronous input data.
set_false_path -from [get_ports reset_n]

# Clocking Wizard supplies the input-clock constraint. Do not add a second
# create_clock here.

