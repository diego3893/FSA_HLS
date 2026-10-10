# Assertions run as ordinary Tcl after opening synthesis or routed designs.
proc check_nm37_clocks {} {
    foreach port {board_clk_p board_clk_n} {
        set p [get_ports $port]
        if {[llength $p]!=1 || [get_property IOSTANDARD $p] ne "DIFF_SSTL12" ||
            [get_property ODT $p] ne "RTT_48"} {
            error "NM37 AC-coupled clock requires DIFF_SSTL12 split ODT RTT_48"
        }
    }
    foreach stack {0 1} {
        set pin [get_pins u_bd/split_d_system_i/hbm_0/inst/HBM_REF_CLK_$stack]
        if {[llength $pin]!=1} { error "Expected one HBM reference per stack" }
        set clk [get_clocks -of_objects $pin]
        if {[llength $clk]!=1 || ![get_property IS_GENERATED $clk] ||
            [get_property MASTER_CLOCK $clk] ne "board_clk_100" ||
            abs([get_property PERIOD $clk]-10.0)>0.001} {
            error "HBM reference must derive from the 100MHz board clock"
        }
    }
    set pin [get_pins u_bd/split_d_system_i/fsa_0/ap_clk]
    if {[llength $pin]!=1} { error "Expected one FSA clock pin" }
    set clk [get_clocks -of_objects $pin]
    if {[llength $clk]!=1 || abs([get_property PERIOD $clk]-10.0)>0.001} {
        error "FSA clock must remain 100MHz"
    }
    puts "NM37_CLOCK_ASSERTIONS_PASS core=$clk"
    return $clk
}
