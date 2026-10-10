
################################################################
# This is a generated script based on design: split_d_system
#
# Though there are limitations about the generated script,
# the main purpose of this utility is to make learning
# IP Integrator Tcl commands easier.
################################################################

namespace eval _tcl {
proc get_script_folder {} {
   set script_path [file normalize [info script]]
   set script_folder [file dirname $script_path]
   return $script_folder
}
}
variable script_folder
set script_folder [_tcl::get_script_folder]

################################################################
# Check if script is running in correct Vivado version.
################################################################
set scripts_vivado_version 2024.2
set current_vivado_version [version -short]

if { [string first $scripts_vivado_version $current_vivado_version] == -1 } {
   puts ""
   if { [string compare $scripts_vivado_version $current_vivado_version] > 0 } {
      catch {common::send_gid_msg -ssname BD::TCL -id 2042 -severity "ERROR" " This script was generated using Vivado <$scripts_vivado_version> and is being run in <$current_vivado_version> of Vivado. Sourcing the script failed since it was created with a future version of Vivado."}

   } else {
     catch {common::send_gid_msg -ssname BD::TCL -id 2041 -severity "ERROR" "This script was generated using Vivado <$scripts_vivado_version> and is being run in <$current_vivado_version> of Vivado. Please run the script in Vivado <$scripts_vivado_version> then open the design in Vivado <$current_vivado_version>. Upgrade the design by running \"Tools => Report => Report IP Status...\", then run write_bd_tcl to create an updated script."}

   }

   return 1
}

################################################################
# START
################################################################

# To test this script, run the following commands from Vivado Tcl console:
# source split_d_system_script.tcl


# The design that will be created by this Tcl script contains the following 
# module references:
# split_d_selftest

# Please add the sources of those modules before sourcing this Tcl script.

# If there is no project opened, this script will create a
# project, but make sure you do not have an existing project
# <./myproj/project_1.xpr> in the current working folder.

set list_projs [get_projects -quiet]
if { $list_projs eq "" } {
   create_project project_1 myproj -part xcvu37p_CIV-fsvh2892-2-e
}


# CHANGE DESIGN NAME HERE
variable design_name
set design_name split_d_system

# If you do not already have an existing IP Integrator design open,
# you can create a design using the following command:
#    create_bd_design $design_name

# Creating design if needed
set errMsg ""
set nRet 0

set cur_design [current_bd_design -quiet]
set list_cells [get_bd_cells -quiet]

if { ${design_name} eq "" } {
   # USE CASES:
   #    1) Design_name not set

   set errMsg "Please set the variable <design_name> to a non-empty value."
   set nRet 1

} elseif { ${cur_design} ne "" && ${list_cells} eq "" } {
   # USE CASES:
   #    2): Current design opened AND is empty AND names same.
   #    3): Current design opened AND is empty AND names diff; design_name NOT in project.
   #    4): Current design opened AND is empty AND names diff; design_name exists in project.

   if { $cur_design ne $design_name } {
      common::send_gid_msg -ssname BD::TCL -id 2001 -severity "INFO" "Changing value of <design_name> from <$design_name> to <$cur_design> since current design is empty."
      set design_name [get_property NAME $cur_design]
   }
   common::send_gid_msg -ssname BD::TCL -id 2002 -severity "INFO" "Constructing design in IPI design <$cur_design>..."

} elseif { ${cur_design} ne "" && $list_cells ne "" && $cur_design eq $design_name } {
   # USE CASES:
   #    5) Current design opened AND has components AND same names.

   set errMsg "Design <$design_name> already exists in your project, please set the variable <design_name> to another value."
   set nRet 1
} elseif { [get_files -quiet ${design_name}.bd] ne "" } {
   # USE CASES: 
   #    6) Current opened design, has components, but diff names, design_name exists in project.
   #    7) No opened design, design_name exists in project.

   set errMsg "Design <$design_name> already exists in your project, please set the variable <design_name> to another value."
   set nRet 2

} else {
   # USE CASES:
   #    8) No opened design, design_name not in project.
   #    9) Current opened design, has components, but diff names, design_name not in project.

   common::send_gid_msg -ssname BD::TCL -id 2003 -severity "INFO" "Currently there is no design <$design_name> in project, so creating one..."

   create_bd_design $design_name

   common::send_gid_msg -ssname BD::TCL -id 2004 -severity "INFO" "Making design <$design_name> as current_bd_design."
   current_bd_design $design_name

}

common::send_gid_msg -ssname BD::TCL -id 2005 -severity "INFO" "Currently the variable <design_name> is equal to \"$design_name\"."

if { $nRet != 0 } {
   catch {common::send_gid_msg -ssname BD::TCL -id 2006 -severity "ERROR" $errMsg}
   return $nRet
}

set bCheckIPsPassed 1
##################################################################
# CHECK IPs
##################################################################
set bCheckIPs 1
if { $bCheckIPs == 1 } {
   set list_check_ips "\ 
xilinx.com:ip:clk_wiz:6.0\
xilinx.com:ip:xlconstant:1.1\
xilinx.com:ip:proc_sys_reset:5.0\
xilinx.com:ip:hbm:1.0\
xilinx.com:hls:fsa_stream_split_d:1.0\
xilinx.com:ip:smartconnect:1.0\
xilinx.com:ip:util_vector_logic:2.0\
"

   set list_ips_missing ""
   common::send_gid_msg -ssname BD::TCL -id 2011 -severity "INFO" "Checking if the following IPs exist in the project's IP catalog: $list_check_ips ."

   foreach ip_vlnv $list_check_ips {
      set ip_obj [get_ipdefs -all $ip_vlnv]
      if { $ip_obj eq "" } {
         lappend list_ips_missing $ip_vlnv
      }
   }

   if { $list_ips_missing ne "" } {
      catch {common::send_gid_msg -ssname BD::TCL -id 2012 -severity "ERROR" "The following IPs are not found in the IP Catalog:\n  $list_ips_missing\n\nResolution: Please add the repository containing the IP(s) to the project." }
      set bCheckIPsPassed 0
   }

}

##################################################################
# CHECK Modules
##################################################################
set bCheckModules 1
if { $bCheckModules == 1 } {
   set list_check_mods "\ 
split_d_selftest\
"

   set list_mods_missing ""
   common::send_gid_msg -ssname BD::TCL -id 2020 -severity "INFO" "Checking if the following modules exist in the project's sources: $list_check_mods ."

   foreach mod_vlnv $list_check_mods {
      if { [can_resolve_reference $mod_vlnv] == 0 } {
         lappend list_mods_missing $mod_vlnv
      }
   }

   if { $list_mods_missing ne "" } {
      catch {common::send_gid_msg -ssname BD::TCL -id 2021 -severity "ERROR" "The following module(s) are not found in the project: $list_mods_missing" }
      common::send_gid_msg -ssname BD::TCL -id 2022 -severity "INFO" "Please add source files for the missing module(s) above."
      set bCheckIPsPassed 0
   }
}

if { $bCheckIPsPassed != 1 } {
  common::send_gid_msg -ssname BD::TCL -id 2023 -severity "WARNING" "Will not continue with creation of design due to the error(s) above."
  return 3
}

##################################################################
# DESIGN PROCs
##################################################################



# Procedure to create entire design; Provide argument to make
# procedure reusable. If parentCell is "", will use root.
proc create_root_design { parentCell } {

  variable script_folder
  variable design_name

  if { $parentCell eq "" } {
     set parentCell [get_bd_cells /]
  }

  # Get object for parentCell
  set parentObj [get_bd_cells $parentCell]
  if { $parentObj == "" } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2090 -severity "ERROR" "Unable to find parent cell <$parentCell>!"}
     return
  }

  # Make sure parentObj is hier blk
  set parentType [get_property TYPE $parentObj]
  if { $parentType ne "hier" } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2091 -severity "ERROR" "Parent <$parentObj> has TYPE = <$parentType>. Expected to be <hier>."}
     return
  }

  # Save current instance; Restore later
  set oldCurInst [current_bd_instance .]

  # Set parent object as current
  current_bd_instance $parentObj


  # Create interface ports

  # Create ports
  set sys_clk_100 [ create_bd_port -dir I -type clk -freq_hz 100000000 sys_clk_100 ]
  set reset_n [ create_bd_port -dir I -type rst reset_n ]
  set_property -dict [ list \
   CONFIG.POLARITY {ACTIVE_LOW} \
 ] $reset_n
  set run_test [ create_bd_port -dir I run_test ]
  set stress_enable [ create_bd_port -dir I stress_enable ]
  set test_busy [ create_bd_port -dir O test_busy ]
  set test_done [ create_bd_port -dir O test_done ]
  set test_pass [ create_bd_port -dir O test_pass ]
  set test_fail [ create_bd_port -dir O test_fail ]
  set fail_code [ create_bd_port -dir O -from 7 -to 0 fail_code ]
  set cases_done [ create_bd_port -dir O -from 5 -to 0 cases_done ]
  set last_case_cycles [ create_bd_port -dir O -from 31 -to 0 last_case_cycles ]
  set debug_pc [ create_bd_port -dir O -from 12 -to 0 debug_pc ]
  set debug_state [ create_bd_port -dir O -from 3 -to 0 debug_state ]
  set actual_word [ create_bd_port -dir O -from 63 -to 0 actual_word ]
  set ctrl_clk [ create_bd_port -dir O -type clk ctrl_clk ]
  set clock_locked [ create_bd_port -dir O clock_locked ]
  set init_done [ create_bd_port -dir O -from 0 -to 0 init_done ]

  # Create instance: clk_wiz_0, and set properties
  set clk_wiz_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:clk_wiz:6.0 clk_wiz_0 ]
  set_property -dict [list \
    CONFIG.CLKOUT1_REQUESTED_OUT_FREQ {100.000} \
    CONFIG.CLKOUT2_REQUESTED_OUT_FREQ {225.000} \
    CONFIG.CLKOUT2_USED {true} \
    CONFIG.NUM_OUT_CLKS {2} \
    CONFIG.PRIM_IN_FREQ {100.000} \
    CONFIG.PRIM_SOURCE {No_buffer} \
    CONFIG.RESET_TYPE {ACTIVE_LOW} \
    CONFIG.USE_LOCKED {true} \
    CONFIG.USE_RESET {true} \
  ] $clk_wiz_0


  # Create instance: zero, and set properties
  set zero [ create_bd_cell -type ip -vlnv xilinx.com:ip:xlconstant:1.1 zero ]
  set_property -dict [list \
    CONFIG.CONST_VAL {0} \
    CONFIG.CONST_WIDTH {1} \
  ] $zero


  # Create instance: rst_100, and set properties
  set rst_100 [ create_bd_cell -type ip -vlnv xilinx.com:ip:proc_sys_reset:5.0 rst_100 ]

  # Create instance: rst_225, and set properties
  set rst_225 [ create_bd_cell -type ip -vlnv xilinx.com:ip:proc_sys_reset:5.0 rst_225 ]

  # Create instance: hbm_0, and set properties
  set hbm_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:hbm:1.0 hbm_0 ]
  set_property -dict [list \
    CONFIG.USER_APB_EN {false} \
    CONFIG.USER_AXI_CLK_FREQ {225} \
    CONFIG.USER_DEBUG_EN {TRUE} \
    CONFIG.USER_HBM_DENSITY {8GB} \
    CONFIG.USER_SAXI_01 {false} \
    CONFIG.USER_SAXI_02 {false} \
    CONFIG.USER_SAXI_03 {false} \
    CONFIG.USER_SAXI_04 {false} \
    CONFIG.USER_SAXI_05 {false} \
    CONFIG.USER_SAXI_06 {false} \
    CONFIG.USER_SAXI_07 {false} \
    CONFIG.USER_SAXI_08 {false} \
    CONFIG.USER_SAXI_09 {false} \
    CONFIG.USER_SAXI_10 {false} \
    CONFIG.USER_SAXI_11 {false} \
    CONFIG.USER_SAXI_12 {false} \
    CONFIG.USER_SAXI_13 {false} \
    CONFIG.USER_SAXI_14 {false} \
    CONFIG.USER_SAXI_15 {false} \
    CONFIG.USER_SAXI_16 {false} \
    CONFIG.USER_SAXI_17 {false} \
    CONFIG.USER_SAXI_18 {false} \
    CONFIG.USER_SAXI_19 {false} \
    CONFIG.USER_SAXI_20 {false} \
    CONFIG.USER_SAXI_21 {false} \
    CONFIG.USER_SAXI_22 {false} \
    CONFIG.USER_SAXI_23 {false} \
    CONFIG.USER_SAXI_24 {false} \
    CONFIG.USER_SAXI_25 {false} \
    CONFIG.USER_SAXI_26 {false} \
    CONFIG.USER_SAXI_27 {false} \
    CONFIG.USER_SAXI_28 {false} \
    CONFIG.USER_SAXI_29 {false} \
    CONFIG.USER_SAXI_30 {false} \
    CONFIG.USER_SAXI_31 {false} \
    CONFIG.USER_XSDB_INTF_EN {TRUE} \
  ] $hbm_0


  # Create instance: fsa_0, and set properties
  set fsa_0 [ create_bd_cell -type ip -vlnv xilinx.com:hls:fsa_stream_split_d:1.0 fsa_0 ]

  # Create instance: selftest_0, and set properties
  set block_name split_d_selftest
  set block_cell_name selftest_0
  if { [catch {set selftest_0 [create_bd_cell -type module -reference $block_name $block_cell_name] } errmsg] } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2095 -severity "ERROR" "Unable to add referenced block <$block_name>. Please add the files for ${block_name}'s definition into the project."}
     return 1
   } elseif { $selftest_0 eq "" } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2096 -severity "ERROR" "Unable to referenced block <$block_name>. Please add the files for ${block_name}'s definition into the project."}
     return 1
   }
  
  # Create instance: control_interconnect, and set properties
  set control_interconnect [ create_bd_cell -type ip -vlnv xilinx.com:ip:smartconnect:1.0 control_interconnect ]
  set_property -dict [list \
    CONFIG.NUM_MI {1} \
    CONFIG.NUM_SI {1} \
  ] $control_interconnect


  # Create instance: memory_ready, and set properties
  set memory_ready [ create_bd_cell -type ip -vlnv xilinx.com:ip:util_vector_logic:2.0 memory_ready ]
  set_property -dict [list \
    CONFIG.C_OPERATION {and} \
    CONFIG.C_SIZE {1} \
  ] $memory_ready


  # Create instance: memory_interconnect, and set properties
  set memory_interconnect [ create_bd_cell -type ip -vlnv xilinx.com:ip:smartconnect:1.0 memory_interconnect ]
  set_property -dict [list \
    CONFIG.NUM_CLKS {2} \
    CONFIG.NUM_MI {1} \
    CONFIG.NUM_SI {5} \
  ] $memory_interconnect


  # Create interface connections
  connect_bd_intf_net -intf_net control_interconnect_M00_AXI [get_bd_intf_pins control_interconnect/M00_AXI] [get_bd_intf_pins fsa_0/s_axi_control]
  connect_bd_intf_net -intf_net fsa_0_m_axi_k_gmem [get_bd_intf_pins fsa_0/m_axi_k_gmem] [get_bd_intf_pins memory_interconnect/S01_AXI]
  connect_bd_intf_net -intf_net fsa_0_m_axi_o_gmem [get_bd_intf_pins fsa_0/m_axi_o_gmem] [get_bd_intf_pins memory_interconnect/S03_AXI]
  connect_bd_intf_net -intf_net fsa_0_m_axi_q_gmem [get_bd_intf_pins fsa_0/m_axi_q_gmem] [get_bd_intf_pins memory_interconnect/S00_AXI]
  connect_bd_intf_net -intf_net fsa_0_m_axi_v_gmem [get_bd_intf_pins fsa_0/m_axi_v_gmem] [get_bd_intf_pins memory_interconnect/S02_AXI]
  connect_bd_intf_net -intf_net memory_interconnect_M00_AXI [get_bd_intf_pins memory_interconnect/M00_AXI] [get_bd_intf_pins hbm_0/SAXI_00]
  connect_bd_intf_net -intf_net selftest_0_M_AXI [get_bd_intf_pins selftest_0/M_AXI] [get_bd_intf_pins memory_interconnect/S04_AXI]
  connect_bd_intf_net -intf_net selftest_0_M_AXIL [get_bd_intf_pins selftest_0/M_AXIL] [get_bd_intf_pins control_interconnect/S00_AXI]

  # Create port connections
  connect_bd_net -net clk_wiz_0_clk_out1  [get_bd_pins clk_wiz_0/clk_out1] \
  [get_bd_pins rst_100/slowest_sync_clk] \
  [get_bd_pins hbm_0/APB_0_PCLK] \
  [get_bd_pins hbm_0/APB_1_PCLK] \
  [get_bd_pins fsa_0/ap_clk] \
  [get_bd_pins selftest_0/clk] \
  [get_bd_pins control_interconnect/aclk] \
  [get_bd_pins memory_interconnect/aclk] \
  [get_bd_ports ctrl_clk]
  connect_bd_net -net clk_wiz_0_clk_out2  [get_bd_pins clk_wiz_0/clk_out2] \
  [get_bd_pins rst_225/slowest_sync_clk] \
  [get_bd_pins hbm_0/AXI_00_ACLK] \
  [get_bd_pins memory_interconnect/aclk1]
  connect_bd_net -net clk_wiz_0_locked  [get_bd_pins clk_wiz_0/locked] \
  [get_bd_pins rst_100/dcm_locked] \
  [get_bd_pins rst_225/dcm_locked] \
  [get_bd_ports clock_locked]
  connect_bd_net -net hbm_0_apb_complete_0  [get_bd_pins hbm_0/apb_complete_0] \
  [get_bd_pins memory_ready/Op1]
  connect_bd_net -net hbm_0_apb_complete_1  [get_bd_pins hbm_0/apb_complete_1] \
  [get_bd_pins memory_ready/Op2]
  connect_bd_net -net memory_ready_Res  [get_bd_pins memory_ready/Res] \
  [get_bd_pins selftest_0/memory_ready] \
  [get_bd_ports init_done]
  connect_bd_net -net reset_n_1  [get_bd_ports reset_n] \
  [get_bd_pins clk_wiz_0/resetn] \
  [get_bd_pins rst_100/ext_reset_in] \
  [get_bd_pins rst_225/ext_reset_in]
  connect_bd_net -net rst_100_interconnect_aresetn  [get_bd_pins rst_100/interconnect_aresetn] \
  [get_bd_pins control_interconnect/aresetn] \
  [get_bd_pins memory_interconnect/aresetn]
  connect_bd_net -net rst_100_peripheral_aresetn  [get_bd_pins rst_100/peripheral_aresetn] \
  [get_bd_pins hbm_0/APB_0_PRESET_N] \
  [get_bd_pins hbm_0/APB_1_PRESET_N] \
  [get_bd_pins fsa_0/ap_rst_n] \
  [get_bd_pins selftest_0/reset_n]
  connect_bd_net -net rst_225_peripheral_aresetn  [get_bd_pins rst_225/peripheral_aresetn] \
  [get_bd_pins hbm_0/AXI_00_ARESET_N]
  connect_bd_net -net run_test_1  [get_bd_ports run_test] \
  [get_bd_pins selftest_0/run_test]
  connect_bd_net -net selftest_0_actual_word  [get_bd_pins selftest_0/actual_word] \
  [get_bd_ports actual_word]
  connect_bd_net -net selftest_0_cases_done  [get_bd_pins selftest_0/cases_done] \
  [get_bd_ports cases_done]
  connect_bd_net -net selftest_0_debug_pc  [get_bd_pins selftest_0/debug_pc] \
  [get_bd_ports debug_pc]
  connect_bd_net -net selftest_0_debug_state  [get_bd_pins selftest_0/debug_state] \
  [get_bd_ports debug_state]
  connect_bd_net -net selftest_0_fail_code  [get_bd_pins selftest_0/fail_code] \
  [get_bd_ports fail_code]
  connect_bd_net -net selftest_0_last_case_cycles  [get_bd_pins selftest_0/last_case_cycles] \
  [get_bd_ports last_case_cycles]
  connect_bd_net -net selftest_0_test_busy  [get_bd_pins selftest_0/test_busy] \
  [get_bd_ports test_busy]
  connect_bd_net -net selftest_0_test_done  [get_bd_pins selftest_0/test_done] \
  [get_bd_ports test_done]
  connect_bd_net -net selftest_0_test_fail  [get_bd_pins selftest_0/test_fail] \
  [get_bd_ports test_fail]
  connect_bd_net -net selftest_0_test_pass  [get_bd_pins selftest_0/test_pass] \
  [get_bd_ports test_pass]
  connect_bd_net -net stress_enable_1  [get_bd_ports stress_enable] \
  [get_bd_pins selftest_0/stress_enable]
  connect_bd_net -net sys_clk_100_1  [get_bd_ports sys_clk_100] \
  [get_bd_pins clk_wiz_0/clk_in1] \
  [get_bd_pins hbm_0/HBM_REF_CLK_0] \
  [get_bd_pins hbm_0/HBM_REF_CLK_1]
  connect_bd_net -net zero_dout  [get_bd_pins zero/dout] \
  [get_bd_pins rst_100/aux_reset_in] \
  [get_bd_pins rst_100/mb_debug_sys_rst] \
  [get_bd_pins rst_225/aux_reset_in] \
  [get_bd_pins rst_225/mb_debug_sys_rst]

  # Create address segments
  assign_bd_address -offset 0x00000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_k_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM00] -force
  assign_bd_address -offset 0x10000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_k_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM01] -force
  assign_bd_address -offset 0x20000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_k_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM02] -force
  assign_bd_address -offset 0x30000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_k_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM03] -force
  assign_bd_address -offset 0x40000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_k_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM04] -force
  assign_bd_address -offset 0x50000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_k_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM05] -force
  assign_bd_address -offset 0x60000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_k_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM06] -force
  assign_bd_address -offset 0x70000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_k_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM07] -force
  assign_bd_address -offset 0x80000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_k_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM08] -force
  assign_bd_address -offset 0x90000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_k_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM09] -force
  assign_bd_address -offset 0xA0000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_k_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM10] -force
  assign_bd_address -offset 0xB0000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_k_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM11] -force
  assign_bd_address -offset 0xC0000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_k_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM12] -force
  assign_bd_address -offset 0xD0000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_k_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM13] -force
  assign_bd_address -offset 0xE0000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_k_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM14] -force
  assign_bd_address -offset 0xF0000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_k_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM15] -force
  assign_bd_address -offset 0x000100000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_k_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM16] -force
  assign_bd_address -offset 0x000110000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_k_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM17] -force
  assign_bd_address -offset 0x000120000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_k_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM18] -force
  assign_bd_address -offset 0x000130000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_k_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM19] -force
  assign_bd_address -offset 0x000140000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_k_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM20] -force
  assign_bd_address -offset 0x000150000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_k_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM21] -force
  assign_bd_address -offset 0x000160000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_k_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM22] -force
  assign_bd_address -offset 0x000170000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_k_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM23] -force
  assign_bd_address -offset 0x000180000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_k_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM24] -force
  assign_bd_address -offset 0x000190000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_k_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM25] -force
  assign_bd_address -offset 0x0001A0000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_k_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM26] -force
  assign_bd_address -offset 0x0001B0000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_k_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM27] -force
  assign_bd_address -offset 0x0001C0000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_k_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM28] -force
  assign_bd_address -offset 0x0001D0000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_k_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM29] -force
  assign_bd_address -offset 0x0001E0000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_k_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM30] -force
  assign_bd_address -offset 0x0001F0000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_k_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM31] -force
  assign_bd_address -offset 0x00000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_o_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM00] -force
  assign_bd_address -offset 0x10000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_o_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM01] -force
  assign_bd_address -offset 0x20000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_o_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM02] -force
  assign_bd_address -offset 0x30000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_o_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM03] -force
  assign_bd_address -offset 0x40000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_o_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM04] -force
  assign_bd_address -offset 0x50000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_o_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM05] -force
  assign_bd_address -offset 0x60000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_o_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM06] -force
  assign_bd_address -offset 0x70000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_o_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM07] -force
  assign_bd_address -offset 0x80000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_o_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM08] -force
  assign_bd_address -offset 0x90000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_o_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM09] -force
  assign_bd_address -offset 0xA0000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_o_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM10] -force
  assign_bd_address -offset 0xB0000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_o_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM11] -force
  assign_bd_address -offset 0xC0000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_o_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM12] -force
  assign_bd_address -offset 0xD0000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_o_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM13] -force
  assign_bd_address -offset 0xE0000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_o_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM14] -force
  assign_bd_address -offset 0xF0000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_o_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM15] -force
  assign_bd_address -offset 0x000100000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_o_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM16] -force
  assign_bd_address -offset 0x000110000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_o_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM17] -force
  assign_bd_address -offset 0x000120000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_o_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM18] -force
  assign_bd_address -offset 0x000130000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_o_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM19] -force
  assign_bd_address -offset 0x000140000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_o_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM20] -force
  assign_bd_address -offset 0x000150000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_o_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM21] -force
  assign_bd_address -offset 0x000160000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_o_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM22] -force
  assign_bd_address -offset 0x000170000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_o_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM23] -force
  assign_bd_address -offset 0x000180000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_o_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM24] -force
  assign_bd_address -offset 0x000190000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_o_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM25] -force
  assign_bd_address -offset 0x0001A0000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_o_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM26] -force
  assign_bd_address -offset 0x0001B0000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_o_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM27] -force
  assign_bd_address -offset 0x0001C0000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_o_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM28] -force
  assign_bd_address -offset 0x0001D0000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_o_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM29] -force
  assign_bd_address -offset 0x0001E0000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_o_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM30] -force
  assign_bd_address -offset 0x0001F0000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_o_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM31] -force
  assign_bd_address -offset 0x00000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_q_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM00] -force
  assign_bd_address -offset 0x10000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_q_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM01] -force
  assign_bd_address -offset 0x20000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_q_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM02] -force
  assign_bd_address -offset 0x30000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_q_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM03] -force
  assign_bd_address -offset 0x40000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_q_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM04] -force
  assign_bd_address -offset 0x50000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_q_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM05] -force
  assign_bd_address -offset 0x60000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_q_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM06] -force
  assign_bd_address -offset 0x70000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_q_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM07] -force
  assign_bd_address -offset 0x80000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_q_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM08] -force
  assign_bd_address -offset 0x90000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_q_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM09] -force
  assign_bd_address -offset 0xA0000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_q_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM10] -force
  assign_bd_address -offset 0xB0000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_q_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM11] -force
  assign_bd_address -offset 0xC0000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_q_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM12] -force
  assign_bd_address -offset 0xD0000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_q_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM13] -force
  assign_bd_address -offset 0xE0000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_q_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM14] -force
  assign_bd_address -offset 0xF0000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_q_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM15] -force
  assign_bd_address -offset 0x000100000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_q_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM16] -force
  assign_bd_address -offset 0x000110000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_q_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM17] -force
  assign_bd_address -offset 0x000120000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_q_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM18] -force
  assign_bd_address -offset 0x000130000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_q_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM19] -force
  assign_bd_address -offset 0x000140000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_q_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM20] -force
  assign_bd_address -offset 0x000150000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_q_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM21] -force
  assign_bd_address -offset 0x000160000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_q_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM22] -force
  assign_bd_address -offset 0x000170000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_q_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM23] -force
  assign_bd_address -offset 0x000180000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_q_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM24] -force
  assign_bd_address -offset 0x000190000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_q_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM25] -force
  assign_bd_address -offset 0x0001A0000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_q_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM26] -force
  assign_bd_address -offset 0x0001B0000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_q_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM27] -force
  assign_bd_address -offset 0x0001C0000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_q_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM28] -force
  assign_bd_address -offset 0x0001D0000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_q_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM29] -force
  assign_bd_address -offset 0x0001E0000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_q_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM30] -force
  assign_bd_address -offset 0x0001F0000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_q_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM31] -force
  assign_bd_address -offset 0x00000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_v_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM00] -force
  assign_bd_address -offset 0x10000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_v_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM01] -force
  assign_bd_address -offset 0x20000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_v_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM02] -force
  assign_bd_address -offset 0x30000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_v_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM03] -force
  assign_bd_address -offset 0x40000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_v_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM04] -force
  assign_bd_address -offset 0x50000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_v_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM05] -force
  assign_bd_address -offset 0x60000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_v_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM06] -force
  assign_bd_address -offset 0x70000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_v_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM07] -force
  assign_bd_address -offset 0x80000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_v_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM08] -force
  assign_bd_address -offset 0x90000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_v_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM09] -force
  assign_bd_address -offset 0xA0000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_v_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM10] -force
  assign_bd_address -offset 0xB0000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_v_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM11] -force
  assign_bd_address -offset 0xC0000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_v_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM12] -force
  assign_bd_address -offset 0xD0000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_v_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM13] -force
  assign_bd_address -offset 0xE0000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_v_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM14] -force
  assign_bd_address -offset 0xF0000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_v_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM15] -force
  assign_bd_address -offset 0x000100000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_v_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM16] -force
  assign_bd_address -offset 0x000110000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_v_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM17] -force
  assign_bd_address -offset 0x000120000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_v_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM18] -force
  assign_bd_address -offset 0x000130000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_v_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM19] -force
  assign_bd_address -offset 0x000140000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_v_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM20] -force
  assign_bd_address -offset 0x000150000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_v_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM21] -force
  assign_bd_address -offset 0x000160000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_v_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM22] -force
  assign_bd_address -offset 0x000170000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_v_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM23] -force
  assign_bd_address -offset 0x000180000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_v_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM24] -force
  assign_bd_address -offset 0x000190000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_v_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM25] -force
  assign_bd_address -offset 0x0001A0000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_v_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM26] -force
  assign_bd_address -offset 0x0001B0000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_v_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM27] -force
  assign_bd_address -offset 0x0001C0000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_v_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM28] -force
  assign_bd_address -offset 0x0001D0000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_v_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM29] -force
  assign_bd_address -offset 0x0001E0000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_v_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM30] -force
  assign_bd_address -offset 0x0001F0000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces fsa_0/Data_m_axi_v_gmem] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM31] -force
  assign_bd_address -offset 0x00000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces selftest_0/M_AXI] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM00] -force
  assign_bd_address -offset 0x10000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces selftest_0/M_AXI] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM01] -force
  assign_bd_address -offset 0x20000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces selftest_0/M_AXI] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM02] -force
  assign_bd_address -offset 0x30000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces selftest_0/M_AXI] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM03] -force
  assign_bd_address -offset 0x40000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces selftest_0/M_AXI] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM04] -force
  assign_bd_address -offset 0x50000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces selftest_0/M_AXI] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM05] -force
  assign_bd_address -offset 0x60000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces selftest_0/M_AXI] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM06] -force
  assign_bd_address -offset 0x70000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces selftest_0/M_AXI] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM07] -force
  assign_bd_address -offset 0x80000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces selftest_0/M_AXI] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM08] -force
  assign_bd_address -offset 0x90000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces selftest_0/M_AXI] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM09] -force
  assign_bd_address -offset 0xA0000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces selftest_0/M_AXI] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM10] -force
  assign_bd_address -offset 0xB0000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces selftest_0/M_AXI] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM11] -force
  assign_bd_address -offset 0xC0000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces selftest_0/M_AXI] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM12] -force
  assign_bd_address -offset 0xD0000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces selftest_0/M_AXI] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM13] -force
  assign_bd_address -offset 0xE0000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces selftest_0/M_AXI] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM14] -force
  assign_bd_address -offset 0xF0000000 -range 0x10000000 -target_address_space [get_bd_addr_spaces selftest_0/M_AXI] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM15] -force
  assign_bd_address -offset 0x00000000 -range 0x00010000 -target_address_space [get_bd_addr_spaces selftest_0/M_AXIL] [get_bd_addr_segs fsa_0/s_axi_control/Reg] -force

  # Exclude Address Segments
  exclude_bd_addr_seg -target_address_space [get_bd_addr_spaces selftest_0/M_AXI] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM16]
  exclude_bd_addr_seg -target_address_space [get_bd_addr_spaces selftest_0/M_AXI] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM17]
  exclude_bd_addr_seg -target_address_space [get_bd_addr_spaces selftest_0/M_AXI] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM18]
  exclude_bd_addr_seg -target_address_space [get_bd_addr_spaces selftest_0/M_AXI] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM19]
  exclude_bd_addr_seg -target_address_space [get_bd_addr_spaces selftest_0/M_AXI] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM20]
  exclude_bd_addr_seg -target_address_space [get_bd_addr_spaces selftest_0/M_AXI] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM21]
  exclude_bd_addr_seg -target_address_space [get_bd_addr_spaces selftest_0/M_AXI] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM22]
  exclude_bd_addr_seg -target_address_space [get_bd_addr_spaces selftest_0/M_AXI] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM23]
  exclude_bd_addr_seg -target_address_space [get_bd_addr_spaces selftest_0/M_AXI] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM24]
  exclude_bd_addr_seg -target_address_space [get_bd_addr_spaces selftest_0/M_AXI] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM25]
  exclude_bd_addr_seg -target_address_space [get_bd_addr_spaces selftest_0/M_AXI] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM26]
  exclude_bd_addr_seg -target_address_space [get_bd_addr_spaces selftest_0/M_AXI] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM27]
  exclude_bd_addr_seg -target_address_space [get_bd_addr_spaces selftest_0/M_AXI] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM28]
  exclude_bd_addr_seg -target_address_space [get_bd_addr_spaces selftest_0/M_AXI] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM29]
  exclude_bd_addr_seg -target_address_space [get_bd_addr_spaces selftest_0/M_AXI] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM30]
  exclude_bd_addr_seg -target_address_space [get_bd_addr_spaces selftest_0/M_AXI] [get_bd_addr_segs hbm_0/SAXI_00/HBM_MEM31]


  # Restore current instance
  current_bd_instance $oldCurInst

  validate_bd_design
  save_bd_design
}
# End of create_root_design()


##################################################################
# MAIN FLOW
##################################################################

create_root_design ""


