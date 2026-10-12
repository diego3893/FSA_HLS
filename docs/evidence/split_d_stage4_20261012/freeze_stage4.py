"""Freeze official old-DUT capture or compare it with a candidate capture."""

import argparse
import hashlib
import json
from pathlib import Path
import re


def read_capture(path):
    log = path.read_text(encoding="utf-8")
    assert "[math-summary] 32/32" in log
    assert "[stage4-summary] 32/32" in log
    assert "[PASS] fsa_stream_split_d: PE=16x16 HEAD_DIM=128" in log
    cases = {}
    for line in log.splitlines():
        match = re.fullmatch(r"\[stage4-(input|word)\] (\S+) (\d+) ([01]) (.+)", line)
        if not match:
            continue
        key = (match[2], int(match[3]), int(match[4]))
        entry = cases.setdefault(key, {"inputs": [{}, {}, {}], "outputs": {}})
        fields = match[5].split()
        if match[1] == "input":
            plane, index, bits = int(fields[0]), int(fields[1]), int(fields[2], 16)
            assert index not in entry["inputs"][plane]
            entry["inputs"][plane][index] = bits
        else:
            index, bits = int(fields[0]), int(fields[1], 16)
            assert index not in entry["outputs"]
            entry["outputs"][index] = bits
    assert len(cases) == 32
    for key, entry in cases.items():
        for plane in range(3):
            assert set(entry["inputs"][plane]) == set(range(key[1]*32))
            entry["inputs"][plane] = [entry["inputs"][plane][index] for index in range(key[1]*32)]
        assert set(entry["outputs"]) == set(range(key[1]*64))
        entry["outputs"] = [entry["outputs"][index] for index in range(key[1]*64)]
    return cases


def digest(words):
    return hashlib.sha256(b"".join(word.to_bytes(8, "little") for word in words)).hexdigest()


parser = argparse.ArgumentParser()
parser.add_argument("baseline", type=Path)
parser.add_argument("--header", type=Path)
parser.add_argument("--manifest", type=Path)
parser.add_argument("--candidate", type=Path)
args = parser.parse_args()
baseline = read_capture(args.baseline)
if args.candidate:
    candidate = read_capture(args.candidate)
    assert candidate.keys() == baseline.keys()
    assert candidate == baseline, "candidate differs from frozen old-DUT input/output bits"
    print(json.dumps({"cases": len(baseline), "words": sum(len(e["outputs"]) for e in baseline.values()), "input_mismatches": 0, "output_mismatches": 0}))
if args.header:
    # A candidate or local stub log must never become the official golden.
    original_log = args.baseline.read_text(encoding="utf-8")
    assert re.search(r"(?m)^ebe529be6f4d03f855add5f3fe745af9874fa9d4$", original_log)
    assert "Finished Command csim_design" in original_log
    inputs, outputs, rows, manifest = [], [], [], []
    for (name, length, causal), entry in baseline.items():
        rows.append(f'    {{"{name}", {length}U, {str(bool(causal)).lower()}, {len(inputs)}U, {len(outputs)}U}},')
        packed_inputs = sum(entry["inputs"], [])
        inputs.extend(packed_inputs)
        outputs.extend(entry["outputs"])
        manifest.append({"name": name, "length": length, "causal": bool(causal), "input_sha256": digest(packed_inputs), "output_sha256": digest(entry["outputs"])})
    def array(name, words):
        lines = [f"static const std::uint64_t {name}[] = {{"]
        for start in range(0, len(words), 4):
            lines.append("    "+", ".join(f"0x{word:016x}ULL" for word in words[start:start+4])+",")
        return "\n".join(lines+["};"])
    header = "\n".join([
        "#ifndef SPLIT_D_STAGE4_BASELINE_HPP", "#define SPLIT_D_STAGE4_BASELINE_HPP", "", "#include <cstdint>", "",
        "// Official Vitis 2024.2 old-DUT ebe529b capture; never regenerate from a candidate.",
        "struct Stage4BitCase{", "    const char* name;", "    unsigned length;", "    bool causal;", "    unsigned input_offset;", "    unsigned output_offset;", "};",
        "static const Stage4BitCase STAGE4_BIT_CASES[] = {", *rows, "};", "static const unsigned STAGE4_BIT_CASE_COUNT = 32U;",
        array("STAGE4_BIT_INPUTS", inputs), array("STAGE4_BIT_WORDS", outputs), "", "#endif", ""])
    args.header.write_text(header, encoding="utf-8", newline="\n")
    if args.manifest:
        args.manifest.write_text(json.dumps({"dut_ref": "ebe529be6f4d03f855add5f3fe745af9874fa9d4", "test_ref": "f7e28398fa0bd8f82c3e8a7f0b01905738547824", "scope": "official Split-D regression baseline, not Chisel equivalence", "capture_sha256": hashlib.sha256(args.baseline.read_bytes()).hexdigest(), "header_sha256_lf": hashlib.sha256(args.header.read_bytes()).hexdigest(), "input_words": len(inputs), "output_words": len(outputs), "cases": manifest}, indent=2)+"\n", encoding="utf-8", newline="\n")
    print(f"Frozen {len(rows)} cases: {len(inputs)} input words, {len(outputs)} output words")
