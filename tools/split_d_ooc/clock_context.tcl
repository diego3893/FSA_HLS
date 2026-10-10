# Explicit OOC assumption, not a claim about the later NM37 system clock.
# Device inventory from auto_hold_retry/clock_sites.tsv: BUFGCE_X0Y48,
# SLR0, clock region X4Y2, central to the auto placement in SLR0.
set clock_site BUFGCE_X0Y48
if {[llength [get_sites -quiet $clock_site]] != 1} { error "Missing OOC clock source site" }
if {[get_slrs -of_objects [get_sites $clock_site]] ne "SLR0"} { error "OOC clock source SLR mismatch" }
set_property HD.CLK_SRC $clock_site [get_ports ap_clk]
puts "OOC_CLOCK_SOURCE_ASSUMPTION $clock_site SLR0 X4Y2"
