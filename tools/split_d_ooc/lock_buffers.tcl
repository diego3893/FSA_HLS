# Reserve only BUFG sites actually selected by the placer/router.
# Call after route so this metadata constraint does not bias the comparison.
set f [open buffer_locations.tsv w]
puts $f [join {cell site loc_fixed} "\t"]
foreach bufg [get_cells -quiet -hier -filter {REF_NAME =~ BUFG*}] {
    set site [get_sites -quiet -of_objects $bufg]
    if {[llength $site] != 1} { error "Unplaced or ambiguous BUFG" }
    set_property LOC $site $bufg
    if {![get_property IS_LOC_FIXED $bufg]} { error "BUFG LOC was not fixed" }
    puts $f [join [list [get_property NAME $bufg] $site [get_property IS_LOC_FIXED $bufg]] "\t"]
}
close $f
