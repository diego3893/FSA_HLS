# Read late, after Clocking Wizard has defined the generated clocks.
set fsa_clock_pin [get_pins -hier -filter {NAME =~ */fsa_0/ap_clk}]
if {[llength $fsa_clock_pin]!=1} { error "Expected one integrated FSA clock pin" }
set core_clock [get_clocks -of_objects $fsa_clock_pin]
if {[llength $core_clock]!=1 || abs([get_property PERIOD $core_clock]-10.0)>0.001} {
    error "Integrated FSA must use a 10ns clock"
}
set_clock_uncertainty -setup 2.700 $core_clock
