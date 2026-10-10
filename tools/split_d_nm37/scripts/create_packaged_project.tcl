set package_root [file normalize [file join [file dirname [info script]] ..]]
source [file join $package_root config project_config.tcl]
set argv [list $formal_ip_dir $project_output_dir]
source [file join $package_root scripts create_project.tcl]
