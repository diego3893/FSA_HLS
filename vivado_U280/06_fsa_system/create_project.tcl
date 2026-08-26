set include_fsa 1
set stage_name "06_fsa_system"
set stage_dir [file normalize [file dirname [info script]]]
source [file join $stage_dir .. common tcl create_system_project.tcl]

