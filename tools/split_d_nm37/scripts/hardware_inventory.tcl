# Read-only target inventory. Does not program, reset, or refresh a device.
open_hw_manager
connect_hw_server -url localhost:3121
set targets [get_hw_targets]
puts "JTAG_TARGET_COUNT=[llength $targets]"
foreach target $targets {
    puts "JTAG_TARGET=$target"
    current_hw_target $target
    open_hw_target
    foreach device [get_hw_devices] {
        puts "JTAG_DEVICE=$device PART=[get_property PART $device]"
    }
    close_hw_target
}
disconnect_hw_server
close_hw_manager
