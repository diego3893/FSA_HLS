# This file is generated from component.xml, exported RTL, HLS sources and
# the C++ testbench. Replace every @...@ placeholder before delivery.

set module_name        "@MODULE_NAME@"
set project_name       "@PROJECT_NAME@"
set target_part        "@TARGET_PART@"

set hls_ip_vlnv        "@HLS_IP_VLNV@"
set hls_instance_name  "@HLS_INSTANCE_NAME@"

set synthesis_top      "@SYNTHESIS_TOP@"
set simulation_top     "@SIMULATION_TOP@"
set simulation_runtime "@SIMULATION_RUNTIME@"

set clock_wizard_name  "@CLOCK_WIZARD_NAME@"
set vio_instance_name  "@VIO_INSTANCE_NAME@"
set ila_instance_name  "@ILA_INSTANCE_NAME@"

set input_clock_mhz    "@INPUT_CLOCK_MHZ@"
set output_clock_mhz   "@OUTPUT_CLOCK_MHZ@"

# One list item per probe, in the exact order used by the generated board top.
set vio_probe_in_widths  {@VIO_PROBE_IN_WIDTHS@}
set vio_probe_out_widths {@VIO_PROBE_OUT_WIDTHS@}
set ila_probe_widths     {@ILA_PROBE_WIDTHS@}
set ila_depth             @ILA_DEPTH@

