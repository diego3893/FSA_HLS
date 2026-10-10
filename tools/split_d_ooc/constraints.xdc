# OOC contract only: synchronous AXI/AXI-Lite peers on the same 100MHz clock.
# 2ns external max delay, 0ns external min delay; no board pin assumptions.
# 2.7ns setup budget preserves the HLS scheduling margin. Hold uses the
# physical clock skew/jitter model; no artificial 2.7ns hold uncertainty.
create_clock -name ap_clk -period 10.000 [get_ports ap_clk]
set_clock_uncertainty -setup 2.700 [get_clocks ap_clk]
set inputs [get_ports -filter {DIRECTION == IN && NAME != ap_clk}]
set outputs [get_ports -filter {DIRECTION == OUT}]
set_input_delay -clock ap_clk -max 2.000 $inputs
set_input_delay -clock ap_clk -min 0.000 $inputs
set_output_delay -clock ap_clk -max 2.000 $outputs
set_output_delay -clock ap_clk -min 0.000 $outputs
# Reset is included in the synchronous boundary contract; no false paths.
