"""Stage 0 only: verify archived evidence and enumerate assumed schedules.

No DUT execution, HLS invocation, floating-point golden, or hardware claim.
Run from any directory with Python 3.12; standard library only.
"""

import csv
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import subprocess
import xml.etree.ElementTree as ET


OUT = Path(__file__).resolve().parent
ROOT = OUT.parents[2]
D, H, LPE, LACC = 4, 16, 3, 7
EVENTS = []


def git(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def write_json(name, value):
    (OUT / name).write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def write_csv(name, rows):
    with (OUT / name).open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def report(path):
    """Read the module summary and loop detail separately from original rpt."""
    text = path.read_text(encoding="utf-8")
    rows = [line.split("|")[1:-1] for line in text.splitlines() if "|" in line]
    rows = [[v.strip() for v in row] for row in rows]
    overall = next(row for row in rows if len(row) == 7 and row[0].isdigit() and row[1].isdigit())
    loops = [row for row in rows if len(row) == 8 and row[0].startswith("-")]
    timing = next(row for row in rows if len(row) == 4 and row[0] == "ap_clk")
    return {
        "path": path.relative_to(ROOT).as_posix(),
        "sha256": digest(path.read_bytes()),
        "module_latency": int(overall[0]),
        "module_interval": overall[4],
        "clock_ns": float(timing[2].split()[0]),
        "loops": [dict(name=r[0], latency=int(r[1]), depth=int(r[3]), ii=int(r[4]), trip=int(r[6])) for r in loops],
    }


def verify_evidence():
    base = ROOT / "build/resume_nm37/ii1_round1_4x4"
    manifest = json.loads((base / "manifest.json").read_text())
    payload_bad = [p for p, h in manifest.items() if digest((base / p).read_bytes()) != h]
    source = json.loads((base / "source_manifest.json").read_text())
    source_bad = [p for p, h in source["files"].items() if digest((ROOT / p).read_bytes().replace(b"\r\n", b"\n")) != h]
    code_diff = git("diff", "73110dc", "HEAD", "--", "src", "include", "tests", "hls", "run_hls.sh")
    assert not payload_bad and not source_bad and not code_diff
    dirs = {
        "73110dc_4": base / "syn/report",
        "ebe529b_4": ROOT / "docs/evidence/split_d_ebe529b_4x4_timing/syn/report",
        "ebe529b_16": ROOT / "docs/evidence/split_d_ebe529b_16x16_timing/syn/report",
    }
    records = {}
    tracked_baseline_checked = 0
    for label, directory in dirs.items():
        records[label] = {}
        for name in ["runPeArray", "runAccumulatorColumns"]:
            records[label][name] = report(directory / (name + "_csynth.rpt"))
        for worker in (0, 1):
            for phase, suffix in [("QK", "109_1_VITIS_LOOP_110_2"), ("ROW_SUM", "162_1"), ("PWL", "300_25"), ("PV", "369_33")]:
                path = directory / f"runQueryBlock_{worker}_Pipeline_VITIS_LOOP_{suffix}_csynth.rpt"
                records[label][f"worker{worker}_{phase}"] = report(path)
        for phase in ["QK", "ROW_SUM", "PWL", "PV"]:
            a, b = (records[label][f"worker{w}_{phase}"] for w in (0, 1))
            assert a["module_latency"] == b["module_latency"] and a["loops"] == b["loops"]
        if label.startswith("ebe529b"):
            for item in records[label].values():
                path = ROOT / item["path"]
                assert path.read_bytes().replace(b"\r\n", b"\n") == git("show", "HEAD:" + item["path"]).replace(b"\r\n", b"\n")
                tracked_baseline_checked += 1
            top_path = directory / "fsa_stream_split_d_csynth.xml"
            assert top_path.read_bytes().replace(b"\r\n", b"\n") == git("show", "HEAD:" + top_path.relative_to(ROOT).as_posix()).replace(b"\r\n", b"\n")
            tracked_baseline_checked += 1
        top = ET.parse(directory / "fsa_stream_split_d_csynth.xml").getroot()
        records[label]["top"] = {
            "clock_ns": top.findtext("PerformanceEstimates/SummaryOfTimingAnalysis/EstimatedClockPeriod"),
            "resources": {e.tag: e.text for e in top.find("AreaEstimates/Resources")},
        }
    log = (base / "solution1.log").read_text()
    codes = {code: len(re.findall(r"WARNING: \[" + re.escape(code) + r"\]", log)) for code in ["HLS 200-880", "HLS 200-871", "HLS 200-1016", "HLS 200-875", "HLS 204-65"]}
    counts = json.loads((base / "hierarchy_counts.json").read_text())
    selected = {k: v for k, v in counts.items() if k.endswith(("runPeArray", "runAccumulatorColumns", "mul_11ns_11ns_22_2_1", "mul_24ns_24ns_48_3_1"))}
    assert sorted(selected.values()) == [2, 2, 4, 16]
    result = {
        "head": git("rev-parse", "HEAD").decode().strip(),
        "branch": git("branch", "--show-current").decode().strip(),
        "tested_source": source["commit"],
        "baseline_commit": git("rev-parse", "ebe529b").decode().strip(),
        "payload_sha_checked": len(manifest), "payload_mismatches": payload_bad,
        "source_sha_checked_LF_normalized": len(source["files"]), "source_mismatches": source_bad,
        "source_diff_73110dc_to_HEAD": code_diff.decode(),
        "tracked_baseline_reports_matching_HEAD": tracked_baseline_checked,
        "warning_counts": codes, "selected_instance_counts": selected,
        "reports": records,
        "scope": "Local static inspection and scheduling enumeration only; no DUT or HLS execution.",
    }
    write_json("baseline_verification.json", result)
    return result


def event(scenario, phase, issue, ready, context=-1, row=-1, key=-1, feature=-1, chunk=-1, requested=0, useful=0, axis="assumed_hardware_cycle"):
    EVENTS.append(dict(scenario=scenario, phase=phase, axis=axis, issue=issue, ready=ready, context=context, row=row, key=key, feature=feature, chunk=chunk, requested_fmas=requested, useful_fmas=useful))


def qk_a(n, feedback, service_ii=1, scenario=None):
    gap = max(n * service_ii, feedback)
    if scenario:
        for f in range(H):
            for c in range(n):
                issue = f * gap + c * service_ii
                event(scenario, "QK", issue, issue + feedback, context=c, feature=f, requested=D * D, useful=D * D)
        for c in range(n):
            tokens = [e for e in EVENTS if e["scenario"] == scenario and e["context"] == c]
            assert [e["feature"] for e in tokens] == list(range(H))
            assert all(a["ready"] <= b["issue"] for a, b in zip(tokens, tokens[1:]))
    return (H - 1) * gap + (n - 1) * service_ii + feedback


def qk_b(hop, q_load, service_ii=1, scenario=None):
    start = 0
    trace = []
    for chunk in range(H // D):
        if scenario:
            event(scenario, "Q_LOAD", start, start + q_load, chunk=chunk)
        for key in range(D):
            for row in range(D):
                issue = start + q_load + key * service_ii + row * hop
                e = dict(issue=issue, ready=issue + hop, key=key, feature=chunk * D + row, row=row)
                trace.append(e)
                if scenario:
                    event(scenario, "QK", **e, chunk=chunk, context=0, requested=D, useful=D)
        start += q_load + (D - 1) * service_ii + D * hop
    for key in range(D):
        tokens = [e for e in trace if e["key"] == key]
        assert [e["feature"] for e in tokens] == list(range(H))
        assert all(a["ready"] <= b["issue"] for a, b in zip(tokens, tokens[1:]))
    assert len({(e["row"], e["issue"]) for e in trace}) == len(trace)
    assert len(trace) * D == D * D * H
    return start


def row_pv(hop, service_ii=1, overlap=False, scenario=None):
    pv_start = service_ii if overlap else D * hop + LACC
    tokens = [("ROW_SUM", -1, 0)] + [("PV", f, pv_start + f * service_ii) for f in range(H)]
    trace = []
    acc_issues = []
    for phase, f, enter in tokens:
        for row in range(D):
            issue = enter + row * hop
            trace.append((row, issue))
            if scenario:
                event(scenario, phase, issue, issue + hop, row=row, key=row, feature=f, requested=D, useful=D)
        acc_issue = enter + D * hop
        acc_issues.append(acc_issue)
        if scenario:
            event(scenario, "L_UPDATE" if phase == "ROW_SUM" else "O_UPDATE", acc_issue, acc_issue + LACC, feature=f, requested=D, useful=D)
    assert len(set(trace)) == len(trace)
    assert min(b - a for a, b in zip(acc_issues, acc_issues[1:])) >= 1
    return pv_start + (H - 1) * service_ii + D * hop + LACC


def current_trace():
    axis = "reported_II_iteration_axis_PE_ready_excludes_caller"
    for f in range(H):
        event("current_73110dc", "QK", 3 * f, 3 * f + LPE, feature=f, requested=D * D, useful=D * D, axis=axis)
    for row in range(D):
        event("current_73110dc", "ROW_SUM", 3 * row, 3 * row + LPE, key=row, requested=D * D, useful=D, axis=axis)
    for piece in range(8):
        event("current_73110dc", "PWL_SCAN", piece, piece + LPE, feature=piece, requested=D * D, useful=0, axis=axis)
    for f in range(H):
        group, c = divmod(f, 8)
        for key in range(D):
            issue = group * (D + 1) * 8 + key * 8 + c
            event("current_73110dc", "PV", issue, issue + LPE, key=key, feature=f, requested=D * D, useful=D, axis=axis)
        issue = group * (D + 1) * 8 + D * 8 + c
        event("current_73110dc", "O_UPDATE", issue, issue + LACC, feature=f, requested=D, useful=D, axis=axis)


def budgets():
    """Count tile demand and a deliberately non-overlapped partial budget.

    Excludes softmax/max/alpha, normalization, output, AXI waits and controller.
    It is neither full attention latency nor measured cycles.
    """
    rows = []
    for nseq in [D - 1, D, D + 1, 2 * D, 2 * D + 1, 3 * D, 4 * D, 5 * D]:
        nt = math.ceil(nseq / D)
        for causal in [False, True]:
            pairs = nseq * (nseq + 1) // 2 if causal else nseq * nseq
            tile_pairs = nt * (nt + 1) // 2 if causal else nt * nt
            for design, capacity in [("ebe529b", 1), ("73110dc", 1), ("B_hop4_qload1", 1), ("C_A_M3", 3), ("C_A_M4", 4), ("C_A_M8", 8)]:
                qk, rspv, cold, loads, batches = 0, 0, 0, 0, 0
                useful_contexts = 0
                for base in range(0, nt, capacity):
                    batches += 1
                    ctx = list(range(base, min(base + capacity, nt)))
                    kt_count = max(ctx) + 1 if causal else nt
                    for kt in range(kt_count):
                        active = [c for c in ctx if not causal or kt <= c]
                        assert active
                        useful_contexts += len(active)
                        loads += 1
                        cold += D * H // 4  # K and V on separate ports in parallel.
                        if design == "ebe529b":
                            qk += 83
                            rspv += 22 + LACC + 89
                        elif design == "73110dc":
                            qk += 52
                            rspv += 15 + LACC + 89
                        elif design.startswith("B_"):
                            qk += qk_b(4, 1)
                            rspv += 1 + row_pv(4)  # One assumed P-bank load cycle.
                        else:
                            qk += qk_a(len(active), 4)
                            rspv += len(active) * (1 + row_pv(4))
                assert useful_contexts == tile_pairs
                rows.append(dict(N=nseq, causal=int(causal), design=design, capacity=capacity, query_tiles=nt, batches=batches, tile_pairs=tile_pairs, semantic_pairs=pairs, useful_QK_MAC=pairs * H, useful_PV_MAC=pairs * H, ordered_ROW_FMA=D * D * tile_pairs, ordered_PV_FMA=D * D * H * tile_pairs, qk_budget=qk, row_pv_budget=rspv, kv_cold_budget=cold, kv_load_events=loads, partial_serial_budget=qk + rspv + cold, Q_words_valid=nseq * H // 4, KV_words_each_port_valid=sum(min(D, nseq - kt * D) * H // 4 for base in range(0, nt, capacity) for kt in range((min(base + capacity, nt) if causal else nt))), O_words_valid=nseq * H // 2))
    return rows


def fixed_input_manifest():
    cases = []
    for nseq in [3, 4, 5, 8, 9, 12, 16, 20]:
        hashes = {}
        for name, a, b, modulus, center, scale in [("Q", 5, 3, 17, 8, 16), ("K", 7, 1, 19, 9, 16), ("V", 3, 5, 23, 11, 8)]:
            data = b"".join(struct.pack("<e", ((a * t + b * f) % modulus - center) / scale) for t in range(nseq) for f in range(H))
            hashes[name] = digest(data)
        for causal in [False, True]:
            cases.append(dict(name=f"stage0-fixed-N{nseq}-causal{int(causal)}", N=nseq, H=H, causal=causal, input_sha256=hashes))
    write_json("fixed_input_manifest.json", {
        "encoding": "token-major, feature-minor, little-endian IEEE FP16; exactly representable dyadic inputs",
        "recipes": {"Q": "((5*token+3*feature)%17-8)/16", "K": "((7*token+feature)%19-9)/16", "V": "((3*token+5*feature)%23-11)/8"},
        "existing_24_case_source_sha256_LF": digest((ROOT / "tests/stream/test_fsa_stream_split_d.cpp").read_bytes().replace(b"\r\n", b"\n")),
        "new_cases": cases,
        "status": "Inputs specified and hashed only. Not added to the testbench or simulated. No output golden generated.",
    })


def main():
    evidence = verify_evidence()
    current_trace()
    sensitivity = []
    for feedback in [3, 4, 5, 6]:
        for capacity in [1, 2, 3, 4, 5, 6, 8]:
            for active in range(1, capacity + 1):
                sensitivity.append(dict(feedback=feedback, capacity=capacity, active=active, compact_cycles=qk_a(active, feedback), allocated_slots_cycles=qk_a(capacity, feedback), compact_cycles_per_context=qk_a(active, feedback) / active))
    for n in [1, 3, 4, 8]:
        qk_a(n, 4, scenario=f"A_active{n}_feedback4")
    b = []
    for hop in [3, 4, 5, 6]:
        for qload in [1, D]:
            b.append(dict(hop=hop, qload=qload, cycles=qk_b(hop, qload, scenario=f"B_hop{hop}_qload{qload}")))
    separate = row_pv(4, scenario="C_ROW_PV_barrier_hop4")
    joint = row_pv(4, overlap=True, scenario="C_ROW_PV_overlap_hop4")
    assert separate == 61 and joint == 39
    # A larger feedback delay cannot be hidden by insufficient independent state.
    assert qk_a(3, 4) > 3 * H and qk_a(4, 4) == 67
    write_csv("schedule_events.csv", EVENTS)
    write_csv("qk_A_sensitivity.csv", sensitivity)
    write_csv("qk_B_sensitivity.csv", b)
    transaction_rows = budgets()
    write_csv("workload_budgets.csv", transaction_rows)
    fixed_input_manifest()
    write_json("model_summary.json", {
        "assumptions": {"D": D, "H": H, "PE_latency_historical": LPE, "PE_service_II_assumed": 1, "feedback_assumed": 4, "hop_assumed": 4, "Accumulator_latency_historical": LACC, "Accumulator_service_II_assumed": 1, "P_bank_load_assumed": 1, "q_load_for_B_optimistic": 1, "R_chunk_extra": 0, "tail_contexts": "compact with stalls when active < feedback", "AXI": "ideal parallel K/V; no compute/load overlap; waits unmodeled"},
        "ROW_PV_barrier": separate, "ROW_PV_overlap": joint,
        "A_full4_QK": qk_a(4, 4), "A_single_QK": qk_a(1, 4),
        "B_hop4_qload1": qk_b(4, 1), "B_hop4_qload4": qk_b(4, 4),
        "context_payload_bytes_excluding_flags": {str(m): m * (6 * D * H + 6 * D * D + 8 * D) for m in [1, 3, 4, 5, 8]},
        "event_rows": len(EVENTS), "workload_rows": len(transaction_rows),
        "checks": ["247 archived payload SHA", "92 LF-normalized source SHA", "both worker report agreement", "A/B feature order and ready-before-read", "B and spatial ROW/PV coordinate ownership", "shared Accumulator issue spacing", "causal batch tile demand", "useful MAC conservation"],
        "limits": "Symbolic schedule checks only. No new numerical or synthesis validation. Partial budgets exclude whole-transaction overhead and cannot certify speedup.",
        "head_at_verification": evidence["head"],
    })
    print(json.dumps({"payload_SHA_pass": evidence["payload_sha_checked"], "source_SHA_pass": evidence["source_sha_checked_LF_normalized"], "schedule_events": len(EVENTS), "workloads": len(transaction_rows), "A4": qk_a(4, 4), "B": qk_b(4, 1), "ROW_PV_barrier": separate, "ROW_PV_overlap": joint}))


if __name__ == "__main__":
    main()
