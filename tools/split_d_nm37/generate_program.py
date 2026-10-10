"""Freeze existing Vitis vectors into the NM37 AXI test program; no HLS changes."""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "tools/split_d_nm37/data"
BASES = {"q": 0x00000000, "k": 0x00100000, "v": 0x00200000, "o": 0x00300000}
CANARY = 0x0000000012345678
# opcode, address32, expected/data64, repeat count20, padding8.
WRITE, CTRL_WRITE, READ, CTRL_CHECK, CTRL_POLL = 1, 2, 3, 4, 5
FILL, CHECK_FILL, CASE_END, END = 6, 7, 8, 15


def main():
    source = ROOT / "docs/evidence/split_d_25d6dda_4x4/baseline.json"
    baseline = json.loads(source.read_text(encoding="utf-8"))
    commands, cases = [], []

    def emit(op, address=0, data=0, count=1):
        assert 0 <= address < 2**32 and 0 <= data < 2**64
        assert 0 < count < 2**20
        commands.append((op << 124) | (address << 92) | (data << 28) | (count << 8))

    for case in baseline["cases"]:
        begin = len(commands)
        valid = "o_words" in case
        inputs = case.get("inputs", {})
        for kind in ("q", "k", "v"):
            for i, word in enumerate(inputs.get(kind, [])):
                emit(WRITE, BASES[kind] + i * 8, int(word, 16))
        output_count = len(case["o_words"]) if valid else case["checked_words"]
        emit(FILL, BASES["o"] - 8, CANARY, output_count + 2)
        for addr, data in ((0x10, BASES["q"]), (0x14, 0),
                           (0x1c, BASES["k"]), (0x20, 0),
                           (0x28, BASES["v"]), (0x2c, 0),
                           (0x34, BASES["o"]), (0x38, 0),
                           (0x40, case["length"]), (0x48, int(case["causal"])),
                           (0x00, 1)):
            emit(CTRL_WRITE, addr, data)
        # Upper 32 bits are the compare mask; lower 32 bits are the value.
        emit(CTRL_POLL, 0x00, (2 << 32) | 2)
        emit(CTRL_CHECK, 0x50, (0xff << 32) | (0 if valid else 1))
        if valid:
            emit(READ, BASES["o"] - 8, CANARY)
            for i, word in enumerate(case["o_words"]):
                emit(READ, BASES["o"] + i * 8, int(word, 16))
            emit(READ, BASES["o"] + output_count * 8, CANARY)
            # The read-only inputs must also survive the transaction unchanged.
            for kind in ("q", "k", "v"):
                for i, word in enumerate(inputs[kind]):
                    emit(READ, BASES[kind] + i * 8, int(word, 16))
        else:
            emit(CHECK_FILL, BASES["o"] - 8, CANARY, output_count + 2)
        emit(CASE_END)
        cases.append({"name": case["name"], "length": case["length"],
                      "causal": case["causal"], "first_pc": begin,
                      "last_pc": len(commands) - 1,
                      "output_words_checked": output_count, "valid": valid})
    emit(END)
    assert len(cases) == 26 and sum(c["valid"] for c in cases) == 24
    assert len(commands) <= 8192
    OUT.mkdir(parents=True, exist_ok=True)
    program = "".join(f"{word:032x}\n" for word in commands)
    # Fill unused ROM locations with END rather than unknown values.
    program += f"{15 << 124 | 1 << 8:032x}\n" * (8192 - len(commands))
    (OUT / "split_d_program.mem").write_text(program, encoding="ascii", newline="\n")
    manifest = {"baseline_commit": baseline["commit"],
                "baseline_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
                "program_sha256": hashlib.sha256(program.encode("ascii")).hexdigest(),
                "commands": len(commands), "rom_depth": 8192,
                "bases": BASES, "cases": cases,
                "scope": "24 exact Vitis vectors, 2 invalid lengths with all 32768 O words and guards; HBM system tests supplement official HLS tests"}
    (OUT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Generated {len(commands)} commands for 26 transactions")


if __name__ == "__main__":
    main()
