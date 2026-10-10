# Source from a new isolated directory. These are controller unit checks only.
set package_dir [file normalize [file join [file dirname [info script]] ..]]
file copy [file join $package_dir sim unit_program.mem] [file join [pwd] unit_program.mem]
exec xvlog --sv [file join $package_dir rtl split_d_selftest.sv] [file join $package_dir sim tb_split_d_selftest.sv]
exec xelab tb_split_d_selftest -s selftest_unit
foreach args {{} {-testplusarg inject} {-testplusarg timeout} {-testplusarg reset}} {
    set result [exec xsim selftest_unit -runall {*}$args]
    puts $result
    if {[string first "SELFTEST UNIT PASS" $result]<0 || [string first "Fatal:" $result]>=0} { error "Controller unit test failed: $args" }
}
puts "ALL_CONTROLLER_UNIT_GATES_PASS"
