# fsa_dma_top NM37 fixed-vector board-test configuration.
set module_name        "fsa_dma_top"
set project_name       "fsa_dma_nm37_board_test"
set target_part        "xcvu37p_CIV-fsvh2892-2-e"

set hls_ip_vlnv        "xilinx.com:hls:fsa_dma_top:1.0"
set hls_instance_name  "fsa_dma_top_0"

set synthesis_top      "fsa_dma_board_top"
set simulation_top     "tb_fsa_dma_control_system"
set simulation_runtime "5ms"

set clock_wizard_name  "fsa_dma_clk_wiz_0"
set vio_instance_name  "fsa_dma_vio_0"
set ila_instance_name  "fsa_dma_ila_0"

set input_clock_mhz     "100.000"
set output_clock_mhz    "100.000"

# VIO input probes, in board-top order:
# busy, done, pass, fail, state, fail_code, status, check_index, timeout_count.
set vio_probe_in_widths  {1 1 1 1 6 8 8 6 32}
# VIO output probe 0 is run_test.
set vio_probe_out_widths {1}

# ILA probes, in board-top order. See the validation guide for meanings.
set ila_probe_widths {1 6 4 7 6 7 4 32 64 4 64 64 6 8 6}
set ila_depth 4096

