## NM37 onboard 100 MHz differential clock: CLK_100_DDR_P/N
set_property PACKAGE_PIN BH42 [get_ports board_clk_p]
set_property PACKAGE_PIN BJ42 [get_ports board_clk_n]
set_property IOSTANDARD DIFF_SSTL12 [get_ports {board_clk_p board_clk_n}]

## NM37 onboard SW2: SLOT_CPU_RESET, active low
set_property PACKAGE_PIN BF2 [get_ports reset_n]
set_property IOSTANDARD LVCMOS18 [get_ports reset_n]

## Asynchronous board reset is not a synchronous data input.
set_false_path -from [get_ports reset_n]

# Do not add create_clock here. Clocking Wizard generates the input-clock
# constraint for this project.

