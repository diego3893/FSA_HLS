#!/usr/bin/env bash
set -euo pipefail

card_index="${1:-0}"
device_h2c="/dev/xdma${card_index}_h2c_0"
device_c2h="/dev/xdma${card_index}_c2h_0"
dma_to_device_bin="${DMA_TO_DEVICE:-dma_to_device}"
dma_from_device_bin="${DMA_FROM_DEVICE:-dma_from_device}"
work_dir="${TMPDIR:-/tmp}/fsa_u280_loopback"
mkdir -p "$work_dir"

for device in "$device_h2c" "$device_c2h"; do
    if [[ ! -e "$device" ]]; then
        echo "ERROR: missing XDMA device: $device" >&2
        exit 1
    fi
done
command -v "$dma_to_device_bin" >/dev/null
command -v "$dma_from_device_bin" >/dev/null

for size in 32 64 4096 65536 1048576; do
    input_file="$work_dir/input_${size}.bin"
    output_file="$work_dir/output_${size}.bin"
    dd if=/dev/urandom of="$input_file" bs="$size" count=1 status=none
    "$dma_to_device_bin" -d "$device_h2c" -a 0x08000000 -s "$size" -f "$input_file"
    "$dma_from_device_bin" -d "$device_c2h" -a 0x08000000 -s "$size" -f "$output_file"
    cmp "$input_file" "$output_file"
    echo "PASS size=$size address=0x08000000"
done

echo "XDMA_HBM_LOOPBACK_PASS"

