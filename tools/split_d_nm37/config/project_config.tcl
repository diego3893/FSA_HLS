# Resolves relative to this portable package; no absolute-path edits needed.
set package_root [file normalize [file join [file dirname [info script]] ..]]
set formal_ip_dir [file join $package_root ip_repo fsa_stream_split_d]
set project_output_dir [file join $package_root vivado_project]
