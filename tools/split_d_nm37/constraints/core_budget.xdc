# Read late, after Clocking Wizard has defined the generated clocks.
# Both HBM references are physically driven by the same board IBUFDS output.
# Replace the IP-scoped primary clocks with divide-by-one generated clocks so
# timing retains the board clock insertion delay and common source relationship.
foreach hbm_stack {0 1} {
    set hbm_ref_pin [get_pins -hier -filter "NAME =~ */hbm_0/inst/HBM_REF_CLK_$hbm_stack"]
    if {[llength $hbm_ref_pin]!=1} { error "Expected one HBM reference per stack" }
    create_generated_clock -name [get_property NAME $hbm_ref_pin] \
        -source [get_ports board_clk_p] -divide_by 1 -combinational $hbm_ref_pin
    set hbm_ref_clock [get_clocks -of_objects $hbm_ref_pin]
    if {[llength $hbm_ref_clock]!=1 ||
        ![get_property IS_GENERATED $hbm_ref_clock] ||
        [get_property MASTER_CLOCK $hbm_ref_clock] ne "board_clk_100" ||
        abs([get_property PERIOD $hbm_ref_clock]-10.0)>0.001} {
        error "HBM reference must derive from the 100MHz board clock"
    }
}
set fsa_clock_pin [get_pins -hier -filter {NAME =~ */fsa_0/ap_clk}]
if {[llength $fsa_clock_pin]!=1} { error "Expected one integrated FSA clock pin" }
set core_clock [get_clocks -of_objects $fsa_clock_pin]
if {[llength $core_clock]!=1 || abs([get_property PERIOD $core_clock]-10.0)>0.001} {
    error "Integrated FSA must use a 10ns clock"
}
set_clock_uncertainty -setup 2.700 $core_clock
