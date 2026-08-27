#!/usr/bin/env python3
import argparse
import glob
import math
import mmap
import os
import pathlib
import struct
import sys
import time

FSA_CTRL_BASE = 0x00000000
STATUS_BASE = 0x00010000
Q_BASE = 0x00000000
K_BASE = 0x01000000
V_BASE = 0x02000000
O_BASE = 0x03000000
HBM_LIMIT = 0x10000000

REG_AP_CTRL = 0x00
REG_Q = 0x10
REG_K = 0x1C
REG_V = 0x28
REG_O = 0x34
REG_LENGTH = 0x40
REG_CAUSAL = 0x48
REG_STATUS = 0x50
REG_STATUS_VALID = 0x54

STATUS_SIGNATURE = 0x46534131
SA_ROWS = 4
DEFAULT_L = 9
OUTPUT_TOLERANCE = 0.18


def find_card(requested):
    if requested is not None:
        return requested
    users = sorted(glob.glob("/dev/xdma*_user"))
    indices = []
    for path in users:
        name = pathlib.Path(path).name
        # Python bundled with older FPGA tool installations can be older than
        # 3.9, where str.removeprefix/removesuffix were introduced.
        middle = name[len("xdma"):-len("_user")]
        if middle.isdigit():
            indices.append(int(middle))
    if len(indices) != 1:
        raise RuntimeError(f"expected one XDMA card, found indices={indices}; use --card")
    return indices[0]


class UserBar:
    def __init__(self, path):
        self.fd = os.open(path, os.O_RDWR | os.O_SYNC)
        self.mapping = mmap.mmap(
            self.fd,
            0x00020000,
            flags=mmap.MAP_SHARED,
            prot=mmap.PROT_READ | mmap.PROT_WRITE,
        )

    def close(self):
        self.mapping.close()
        os.close(self.fd)

    def read32(self, offset):
        return struct.unpack_from("<I", self.mapping, offset)[0]

    def write32(self, offset, value):
        struct.pack_into("<I", self.mapping, offset, value & 0xFFFFFFFF)

    def write64(self, offset, value):
        self.write32(offset, value & 0xFFFFFFFF)
        self.write32(offset + 4, (value >> 32) & 0xFFFFFFFF)


def padded(data, alignment=32, fill=0):
    size = ((len(data) + alignment - 1) // alignment) * alignment
    return data + bytes([fill]) * (size - len(data))


def dma_write(path, address, data):
    fd = os.open(path, os.O_WRONLY)
    try:
        written = os.pwrite(fd, data, address)
    finally:
        os.close(fd)
    if written != len(data):
        raise RuntimeError(f"short H2C write: expected={len(data)} actual={written}")


def dma_read(path, address, size):
    fd = os.open(path, os.O_RDONLY)
    try:
        data = os.pread(fd, size, address)
    finally:
        os.close(fd)
    if len(data) != size:
        raise RuntimeError(f"short C2H read: expected={size} actual={len(data)}")
    return data


def make_matrices(length):
    q = [[((token + 2 * feature) % 5 - 2) * 0.25 for feature in range(SA_ROWS)]
         for token in range(length)]
    k = [[((2 * token + feature) % 7 - 3) * 0.25 for feature in range(SA_ROWS)]
         for token in range(length)]
    v = [[((token + feature) % 6 - 2) * 0.5 for feature in range(SA_ROWS)]
         for token in range(length)]
    return q, k, v


def pack_fp16_matrix(matrix):
    result = bytearray()
    for row in matrix:
        for value in row:
            result.extend(struct.pack("<e", value))
    return bytes(result)


def golden_attention(q, k, v, causal):
    length = len(q)
    output = [[0.0] * SA_ROWS for _ in range(length)]
    scale = math.sqrt(SA_ROWS)
    for query in range(length):
        legal_keys = range(query + 1) if causal else range(length)
        scores = []
        for key in legal_keys:
            scores.append(sum(q[query][d] * k[key][d] for d in range(SA_ROWS)))
        row_max = max(scores)
        weights = [math.exp((score - row_max) / scale) for score in scores]
        denominator = sum(weights)
        for index, key in enumerate(legal_keys):
            probability = weights[index] / denominator
            for feature in range(SA_ROWS):
                output[query][feature] += probability * v[key][feature]
    return output


def unpack_fp32_matrix(data, length):
    values = struct.unpack_from("<" + "f" * (length * SA_ROWS), data)
    return [list(values[row * SA_ROWS:(row + 1) * SA_ROWS]) for row in range(length)]


def wait_board_ready(bar, timeout_seconds):
    signature = bar.read32(STATUS_BASE + 0x00)
    if signature != STATUS_SIGNATURE:
        raise RuntimeError(f"status signature mismatch: 0x{signature:08x}")
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        status = bar.read32(STATUS_BASE + 0x04)
        hbm_done = bool(status & 0x1)
        link_up = bool(status & 0x2)
        cattrip = bool(status & 0x4)
        if cattrip:
            raise RuntimeError("HBM CATTRIP is asserted")
        if hbm_done and link_up:
            print(f"BOARD_READY status=0x{status:08x}")
            return
        time.sleep(0.05)
    raise TimeoutError("HBM initialization or XDMA link did not become ready")


def run_fsa(bar, h2c_path, c2h_path, length, causal, timeout_seconds):
    if length <= 0 or length > 4096:
        raise ValueError("length must be in 1..4096")
    q, k, v = make_matrices(length)
    q_data = padded(pack_fp16_matrix(q))
    k_data = padded(pack_fp16_matrix(k))
    v_data = padded(pack_fp16_matrix(v))
    logical_output_bytes = length * SA_ROWS * 4
    output_transfer_bytes = ((logical_output_bytes + 63) // 64) * 64 + 64
    output_canary = bytes([0xA5]) * output_transfer_bytes

    for base, payload, label in ((Q_BASE, q_data, "Q"), (K_BASE, k_data, "K"),
                                 (V_BASE, v_data, "V"), (O_BASE, output_canary, "O canary")):
        if base + len(payload) > HBM_LIMIT:
            raise ValueError(f"{label} exceeds AXI_00 range")
        dma_write(h2c_path, base, payload)
        print(f"H2C_OK label={label} address=0x{base:08x} bytes={len(payload)}")

    ap_ctrl = bar.read32(FSA_CTRL_BASE + REG_AP_CTRL)
    if not (ap_ctrl & 0x4):
        raise RuntimeError(f"fsa_dma_top is not idle: AP_CTRL=0x{ap_ctrl:08x}")
    bar.write64(FSA_CTRL_BASE + REG_Q, Q_BASE)
    bar.write64(FSA_CTRL_BASE + REG_K, K_BASE)
    bar.write64(FSA_CTRL_BASE + REG_V, V_BASE)
    bar.write64(FSA_CTRL_BASE + REG_O, O_BASE)
    bar.write32(FSA_CTRL_BASE + REG_LENGTH, length)
    bar.write32(FSA_CTRL_BASE + REG_CAUSAL, int(causal))
    bar.write32(FSA_CTRL_BASE + REG_AP_CTRL, 0x1)

    deadline = time.monotonic() + timeout_seconds
    done_value = None
    while time.monotonic() < deadline:
        value = bar.read32(FSA_CTRL_BASE + REG_AP_CTRL)
        if value & 0x2:
            done_value = value
            break
        time.sleep(0.001)
    if done_value is None:
        raise TimeoutError("fsa_dma_top did not assert ap_done")

    status_valid = bar.read32(FSA_CTRL_BASE + REG_STATUS_VALID) & 0x1
    status = bar.read32(FSA_CTRL_BASE + REG_STATUS) & 0xFF
    if not status_valid:
        raise RuntimeError("status_ap_vld was not asserted")
    if status != 0:
        raise RuntimeError(f"fsa_dma_top returned status={status}")

    output_data = dma_read(c2h_path, O_BASE, output_transfer_bytes)
    actual = unpack_fp32_matrix(output_data, length)
    expected = golden_attention(q, k, v, causal)
    max_error = 0.0
    for row in range(length):
        for column in range(SA_ROWS):
            value = actual[row][column]
            error = abs(value - expected[row][column])
            max_error = max(max_error, error)
            if not math.isfinite(value) or error > OUTPUT_TOLERANCE:
                raise RuntimeError(
                    f"output mismatch row={row} column={column} actual={value} "
                    f"expected={expected[row][column]} error={error}"
                )
    tail = output_data[logical_output_bytes:]
    if tail != bytes([0xA5]) * len(tail):
        raise RuntimeError("output canary changed after the logical matrix")
    print(f"FSA_BOARD_TEST_PASS L={length} causal={int(causal)} max_error={max_error:.6f}")


def run_invalid_length(bar, h2c_path, c2h_path, invalid_length, timeout_seconds):
    canary = bytes([0x5A]) * 64
    dma_write(h2c_path, O_BASE, canary)
    bar.write64(FSA_CTRL_BASE + REG_Q, Q_BASE)
    bar.write64(FSA_CTRL_BASE + REG_K, K_BASE)
    bar.write64(FSA_CTRL_BASE + REG_V, V_BASE)
    bar.write64(FSA_CTRL_BASE + REG_O, O_BASE)
    bar.write32(FSA_CTRL_BASE + REG_LENGTH, invalid_length)
    bar.write32(FSA_CTRL_BASE + REG_CAUSAL, 0)
    bar.write32(FSA_CTRL_BASE + REG_AP_CTRL, 0x1)
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        if bar.read32(FSA_CTRL_BASE + REG_AP_CTRL) & 0x2:
            break
        time.sleep(0.001)
    else:
        raise TimeoutError("invalid-length transaction did not complete")
    status_valid = bar.read32(FSA_CTRL_BASE + REG_STATUS_VALID) & 0x1
    status = bar.read32(FSA_CTRL_BASE + REG_STATUS) & 0xFF
    output = dma_read(c2h_path, O_BASE, len(canary))
    if not status_valid or status != 1:
        raise RuntimeError(
            f"invalid-length status mismatch: valid={status_valid} status={status}"
        )
    if output != canary:
        raise RuntimeError("invalid-length transaction modified O")
    print(f"INVALID_LENGTH_TEST_PASS L={invalid_length} status={status}")


def main():
    parser = argparse.ArgumentParser(description="U280 XDMA+HBM+fsa_dma_top board test")
    parser.add_argument("--card", type=int)
    parser.add_argument("--length", type=int, default=DEFAULT_L)
    parser.add_argument("--causal", action="store_true")
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument("--status-only", action="store_true")
    parser.add_argument("--invalid-length", type=int, choices=(0, 4097))
    parser.add_argument("--repeat", type=int, default=1)
    args = parser.parse_args()

    card = find_card(args.card)
    user_path = f"/dev/xdma{card}_user"
    h2c_path = f"/dev/xdma{card}_h2c_0"
    c2h_path = f"/dev/xdma{card}_c2h_0"
    for path in (user_path, h2c_path, c2h_path):
        if not os.path.exists(path):
            raise RuntimeError(f"missing device node: {path}")
    print(f"USING_CARD={card} user={user_path} h2c={h2c_path} c2h={c2h_path}")

    bar = UserBar(user_path)
    try:
        wait_board_ready(bar, args.timeout)
        if args.invalid_length is not None:
            run_invalid_length(bar, h2c_path, c2h_path, args.invalid_length, args.timeout)
        elif not args.status_only:
            if args.repeat <= 0:
                raise ValueError("--repeat must be positive")
            for iteration in range(args.repeat):
                print(f"RUN_INDEX={iteration + 1}/{args.repeat}")
                run_fsa(bar, h2c_path, c2h_path, args.length, args.causal, args.timeout)
    finally:
        bar.close()


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
