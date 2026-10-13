"""Report-derived shared-compute budget; excludes full AXI/controller latency."""

import argparse
import csv
import json
from pathlib import Path


parser = argparse.ArgumentParser()
parser.add_argument("summary", type=Path)
parser.add_argument("--out", type=Path, default=Path(__file__).resolve().parent)
args = parser.parse_args()
summary = json.loads(args.summary.read_text())
assert summary.get("csynth_completed", False), "CSynth incomplete; no accepted performance budget"
assert summary["run_exit"] == 0
assert (summary["D"], summary["H"], summary["L_MAX"]) == (16, 128, 1024)
assert summary["stage4_cases"] == 32 and summary["stage4_words"] == 85824
assert "[stage4-summary] 32/32" in summary["summaries"]
assert "[math-summary] 32/32" in summary["summaries"]
reports = summary["reports"]
shared = reports["runSharedEngine_csynth.xml"]
loop = next(value for name, value in reports.items() if name.startswith("runSharedEngine_Pipeline_"))
assert loop["PipelineII"] == ["1"]
assert reports["runPeArray_csynth.xml"]["Interval-min"] == ["1"]
assert reports["runAccumulatorColumns_csynth.xml"]["Interval-min"] == ["1"]
assert not summary["ii_violations"] and not summary["critical_paths"]
assert max(float(value) for value in reports["fsa_stream_split_d_csynth.xml"]["EstimatedClockPeriod"]) <= 7.3
for resource in ["LUT", "FF", "DSP", "BRAM_18K", "URAM"]:
    values = list(map(int, reports["fsa_stream_split_d_csynth.xml"][resource]))
    assert values[0] <= values[1], f"{resource} exceeds target device capacity"
assert summary["rtl_counts"]["fsa_stream_split_d_mul_11ns_11ns_22_2_1"] == 256
assert summary["rtl_counts"]["fsa_stream_split_d_mul_24ns_24ns_48_3_1"] == 16
assert sum(count for name, count in summary["rtl_counts"].items()
           if "_fsub_32ns_32ns_32_" in name) == 32
D, H = 16, 128
M, hop, feedback, acc = (summary[key] for key in ["contexts", "pe_hop", "qk_feedback", "acc_hop"])
overhead = int(shared["Best-caseLatency"][0])-1
assert int(shared["Worst-caseLatency"][0])-int(shared["Best-caseLatency"][0]) == int(loop["Worst-caseLatency"][0])-int(loop["Best-caseLatency"][0])
root = Path(__file__).resolve().parents[3]
old = json.loads((root/"docs/evidence/split_d_ebe529b_16x16_timing/summary.json").read_text())["report_summary"]


def old_cycles(name):
    return int(old[name]["overall"]["Best-caseLatency"])


# The old eight calls: QK, alpha scale/PWL, SUB/SCALE, PWL, ROW_SUM/L, PV/O.
old_tile = sum(old_cycles("runQueryBlock_0_Pipeline_"+suffix) for suffix in [
    "VITIS_LOOP_109_1_VITIS_LOOP_110_2", "VITIS_LOOP_162_1", "VITIS_LOOP_300_25", "VITIS_LOOP_369_33"])
old_tile += 2*old_cycles("runPeArray")+3*old_cycles("runAccumulatorColumns")


def calls(n, first_key):
    gap = max(n, feedback)
    result = {
        "qk": (H-1)*gap+n+hop+overhead,
        "alpha_scale": n+acc+overhead,
        "alpha_pwl": n+acc+overhead,
        "sub_max": n+hop+overhead,
        "scale": n+hop+overhead,
        "pwl": 8*n+hop+overhead,
        "row_sum_l": n+D*hop+acc+overhead,
        "pv_o": H*n+D*hop+acc+overhead,
    }
    if first_key:
        result["alpha_scale"] = result["alpha_pwl"] = 0
    return result


phase_rows = []
for active in range(1, M+1):
    phase = calls(active, False)
    first = calls(active, True)
    phase_rows.append({"active_contexts": active,
                       "qk_feedback_gap": max(active, feedback),
                       "qk_tokens": H*active,
                       "qk_internal_holes": (H-1)*max(0, feedback-active),
                       "row_sum_tokens": active, "pv_tokens": H*active,
                       "first_key_shared_compute_cycles": sum(first.values()),
                       "later_key_shared_compute_cycles": sum(phase.values()),
                       "later_key_cycles_per_query_tile": sum(phase.values())/active,
                       **phase})
with (args.out/"phase_budget.csv").open("w", newline="", encoding="utf-8") as stream:
    writer = csv.DictWriter(stream, fieldnames=list(phase_rows[0]))
    writer.writeheader()
    writer.writerows(phase_rows)


rows = []
for length in sorted(set([1, 2, 5, 15, 16, 17, 32, 33, (M-1)*D, M*D, (M+1)*D])):
    if length <= 0:
        continue
    tiles = (length+D-1)//D
    remainder = tiles % M
    batches = [remainder or M]+[M]*((tiles-1)//M)
    for causal in [False, True]:
        total, kv, kv_tokens, holes, base = 0, 0, 0, 0, 0
        phases = dict.fromkeys(calls(1, False), 0)
        legal_pairs = tiles*(tiles+1)//2 if causal else tiles*tiles
        for size in batches:
            key_count = base+size if causal else tiles
            kv += key_count
            for key in range(key_count):
                kv_tokens += min(D, length-key*D)
                first = max(0, key-base) if causal else 0
                active = size-first
                phase = calls(active, key==0)
                for name, cycles in phase.items():
                    phases[name] += cycles
                total += sum(phase.values())
                holes += (H-1)*max(0, feedback-active)
            base += size
        assert base == tiles and total == sum(phases.values())
        useful_pairs = length*(length+1)//2 if causal else length*length
        rows.append({"N": length, "causal": int(causal), "batches": "+".join(map(str, batches)),
                     "legacy_shared_compute_cycles": legal_pairs*old_tile,
                     "candidate_shared_compute_cycles": total,
                     "change_percent": round(100*(total/(legal_pairs*old_tile)-1), 3),
                     "legal_tile_pairs": legal_pairs, "kv_tile_loads": kv,
                     "useful_qk_mac": useful_pairs*H, "useful_pv_mac": useful_pairs*H,
                     "useful_row_sum_mac": useful_pairs,
                     "ordered_qk_mac_including_padding": legal_pairs*D*D*H,
                     "ordered_pv_mac_including_padding": legal_pairs*D*D*H,
                     "q_axi_words64": length*H//4,
                     "k_axi_words64": kv_tokens*H//4, "v_axi_words64": kv_tokens*H//4,
                     "q_fifo_words64_per_worker": tiles*(D//2)*H//4,
                     "kv_fifo_packets128_per_worker": kv*D*H//4,
                     "o_fifo_words32_per_worker": tiles*(D//2)*H,
                     "o_axi_words64": length*H//2, "qk_internal_holes": holes,
                     "normalize_separate_cycles": tiles*H+len(batches)*(acc+overhead),
                     "reciprocal_vectors_per_worker": tiles, **phases})
with (args.out/"sequence_model.csv").open("w", newline="", encoding="utf-8") as stream:
    writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
    writer.writeheader()
    writer.writerows(rows)
payload_per_context = 2*D*H+2*D*D+4*D*D+4*D*H+3*4*D
ring_bits = D*D*hop*(32+16+1)+2*D*hop*65+D*acc*32+2*acc*65
manifest = {"tested_commit": summary["commit"], "scope": "HLS model of eight shared compute calls; not full attention transaction or hardware measurement",
            "M": M, "hop": hop, "feedback": feedback, "acc_hop": acc,
            "actual_shared_II": 1, "call_overhead": overhead, "legacy_tile_shared_compute_cycles": old_tile,
            "numerical_state_bytes_per_context": payload_per_context,
            "numerical_state_bytes": M*payload_per_context,
            "state_scope": "Persistent source-level arrays; temporary vectors, operand/result registers, arithmetic pipelines, RAM packing and implementation logic are represented only by actual synthesis resources.",
            "flag_bits": M*(2*D*D+2*D), "duplicated_kv_bytes": 2*4*D*H,
            "ring_bits": ring_bits, "ring_bytes_logical_ceiling": ring_bits/8,
            "source_fifo_payload_bits": 2*(2*161+8*64+8*128+8*32),
            "fifo_scope": "Only explicit header/Q/KV/O payload FIFOs; excludes compiler-generated start/control FIFOs and implementation padding.",
            "io_scope": "External AXI word counts omit padded tokens; internal FIFO counts include zero padding and are per worker. AXI bursts and stalls are not modeled.",
            "mac_scope": "Useful MACs and logically ordered valid-token FMAs including padding. Invalid-cycle evaluations discarded by the shared engine are not counted; no power or total switching claim.",
            "excluded": ["max/quantize/controller cycles", "Q/K/V AXI burst or stall cycles", "distributor/worker/gather DATAFLOW wait", "reciprocal latency varies with input", "normalization included only as separate budget", "output packing/store and AXI completion"],
            "phase_rows": phase_rows, "rows": rows}
(args.out/"model_manifest.json").write_text(json.dumps(manifest, indent=2)+"\n", encoding="utf-8")
print(json.dumps({key: manifest[key] for key in ["M", "call_overhead", "numerical_state_bytes", "legacy_tile_shared_compute_cycles"]}))
