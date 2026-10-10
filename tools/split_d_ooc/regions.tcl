# Exact occupied bounding boxes from the auto_context placement.
# primitive_locations.tsv SHA256: c8dc786ddbdf23629dae25c174552e2ed60ee13bd65f8037d5485bd01c228e51
# Hard placement regions, routing not contained, no forced SLR split.
# Bounding boxes may overlap; this does not reserve separate slices.
set worker [get_cells -hier -filter {NAME =~ */runQueryBlock_0_U0}]
if {[llength $worker] != 1} { error "Worker hierarchy mismatch" }
create_pblock query_block_0
add_cells_to_pblock [get_pblocks query_block_0] $worker
resize_pblock [get_pblocks query_block_0] -add {SLICE_X61Y84:SLICE_X116Y238 DSP48E2_X9Y50:DSP48E2_X16Y86 RAMB18_X5Y70:RAMB18_X7Y81}
set_property CONTAIN_ROUTING 0 [get_pblocks query_block_0]
set_property IS_SOFT 0 [get_pblocks query_block_0]
if {[get_property IS_SOFT [get_pblocks query_block_0]] != 0} { error "Hard Pblock not applied" }
set worker [get_cells -hier -filter {NAME =~ */runQueryBlock_1_U0}]
if {[llength $worker] != 1} { error "Worker hierarchy mismatch" }
create_pblock query_block_1
add_cells_to_pblock [get_pblocks query_block_1] $worker
resize_pblock [get_pblocks query_block_1] -add {SLICE_X94Y44:SLICE_X156Y233 DSP48E2_X16Y30:DSP48E2_X19Y73 RAMB18_X7Y56:RAMB18_X8Y74}
set_property CONTAIN_ROUTING 0 [get_pblocks query_block_1]
set_property IS_SOFT 0 [get_pblocks query_block_1]
if {[get_property IS_SOFT [get_pblocks query_block_1]] != 0} { error "Hard Pblock not applied" }
