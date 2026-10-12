"""Compare exact HLS summaries and reproduce stage3 batch/compute budgets.

No DUT, HLS or RTL execution. All budgets exclude controller, AXI stalls,
max/quantization, reciprocal, normalization and output transactions.
"""

import argparse
import csv
import json
from pathlib import Path


OUT = Path(__file__).resolve().parent
D, H, M, PE_HOP, FEEDBACK, ACC_HOP = 4, 16, 8, 7, 8, 11


def batches(tiles, front):
    if front:
        first = tiles % M or M
        return [first]+[M]*((tiles-first)//M)
    return [M]*(tiles//M)+([tiles % M] if tiles % M else [])


def phase_cycles(n):
    return {"QK": (H-1)*max(n, FEEDBACK)+n+PE_HOP,
            "ALPHA_SCALE": n+ACC_HOP, "ALPHA_PWL": n+ACC_HOP,
            "SUB_MAX": n+PE_HOP, "SCALE": n+PE_HOP,
            "PWL": 8*n+PE_HOP, "ROW_SUM": n+D*PE_HOP+ACC_HOP,
            "PV": n*H+D*PE_HOP+ACC_HOP}


def validate(path):
    summary = json.loads(path.read_text(encoding="utf-8-sig"))
    assert (summary["contexts"], summary["pe_hop"], summary["qk_feedback"], summary["acc_hop"]) == (M, PE_HOP, FEEDBACK, ACC_HOP)
    assert summary["summaries"] == ["[stage12-summary] 28/28", "[math-summary] 52/52", "[bit-summary] 24/24"]
    reports = summary["reports"]
    top = reports["fsa_stream_split_d_csynth.xml"]
    assert float(top["EstimatedClockPeriod"][0]) <= 7.300
    assert summary["rtl_counts"]["fsa_stream_split_d_mul_11ns_11ns_22_2_1"] == 16
    assert summary["rtl_counts"]["fsa_stream_split_d_mul_24ns_24ns_48_3_1"] == 4
    pipeline = next(v for k, v in reports.items() if "runSharedEngine_Pipeline" in k)
    assert pipeline["PipelineII"] == ["1"]
    assert not summary["ii_violations"]
    module = reports["runSharedEngine_csynth.xml"]
    low, high = int(module["Best-caseLatency"][0]), int(module["Worst-caseLatency"][0])
    assert high-low == H*M+D*PE_HOP+ACC_HOP-1
    return {"commit": summary["commit"], "clock_ns": float(top["EstimatedClockPeriod"][0]),
            "overhead": low-1, "depth": pipeline["PipelineDepth"],
            "resources": {key: int(top[key][0]) for key in ("DSP", "FF", "LUT", "BRAM_18K", "URAM")},
            "warning_counts": summary["warning_counts"]}


def budget(length, causal, front, skip_initial_alpha, overhead):
    tiles = (length+D-1)//D
    counts = batches(tiles, front)
    base = calls = pairs = holes = loads = kv_words = saved = 0
    for count in counts:
        end = base+count
        for key in range(end if causal else tiles):
            n = count-max(0, key-base) if causal else count
            p = phase_cycles(n)
            full = sum(p.values())+8*overhead
            discount = 2*(n+ACC_HOP+overhead) if skip_initial_alpha and key == 0 else 0
            saved += discount
            calls += full-discount
            pairs += n
            holes += (H-1)*(max(n, FEEDBACK)-n)
            loads += 1
            kv_words += min(D, length-key*D)*H//4
        base = end
    assert pairs == (tiles*(tiles+1)//2 if causal else tiles*tiles)
    return {"partition": "+".join(map(str, counts)), "legal_tile_pairs": pairs,
            "compute_call_budget": calls, "QK_internal_holes": holes,
            "KV_load_events": loads, "K_and_V_AXI_words_each": kv_words,
            "initial_alpha_call_budget_saved": saved}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("baseline", type=Path)
    parser.add_argument("tail", type=Path)
    parser.add_argument("--final", type=Path)
    args = parser.parse_args()
    versions = {"stage2": validate(args.baseline), "tail_only": validate(args.tail)}
    if args.final:
        versions["final"] = validate(args.final)
    # Independent DP minimizes (batch count, sum of cumulative endpoints).
    # Source controller must use exactly the front-loaded partition tested here.
    optimum = [(0, 0)]
    for tiles in range(1, 1025):
        optimum.append(min((optimum[tiles-count][0]+1,
                            optimum[tiles-count][1]+tiles)
                           for count in range(1, min(M, tiles)+1)))
        counts = batches(tiles, True)
        endpoint = total = 0
        for count in counts:
            endpoint += count
            total += endpoint
        assert (len(counts), total) == optimum[tiles]
    rows = []
    for length in (3, 4, 5, 8, 9, 12, 16, 20, 24, 28, 32, 36, 64, 68, 128):
        for causal in (False, True):
            baseline = budget(length, causal, False, False, versions["stage2"]["overhead"])
            for name, evidence in versions.items():
                result = budget(length, causal, name != "stage2", name == "final", evidence["overhead"])
                rows.append({"version": name, "commit": evidence["commit"], "N": length,
                             "causal": int(causal), **result,
                             "stage2_budget": baseline["compute_call_budget"],
                             "delta_percent": round(100*(result["compute_call_budget"]/baseline["compute_call_budget"]-1), 3),
                             "scope": "HLS compute calls only; no RTL or hardware measurement"})
    with (OUT / "sequence_ablation.csv").open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    manifest = {"scope": "HLS schedule prediction; not complete transaction latency",
                "versions": versions, "front_partition_DP_verified_tiles": [1, 1024],
                "state_payload_bytes": {"stage2_per_context": 576, "final_per_context": 528,
                                        "stage2_M8": 4608, "final_M8": 4224},
                "fixed_KV_bytes": 512, "ring_payload_ceiling_bytes": 1495.75,
                "FIFO_depths_unchanged": {"header": 2, "Q": 8, "KV": 8, "O": 8},
                "minimum_full_QK_issue_contexts": FEEDBACK,
                "example_N36_causal": [row for row in rows if row["N"] == 36 and row["causal"]]}
    (OUT / "ablation_manifest.json").write_text(json.dumps(manifest, indent=2)+"\n", encoding="utf-8")
    print(json.dumps(manifest, indent=2))


if __name__ == "__main__":
    main()
