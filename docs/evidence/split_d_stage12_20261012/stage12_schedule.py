"""Reproduce the accepted 4/16 schedule; no DUT, HLS or hardware execution.

Use the exact official default-M8 summary. Budgets cover named compute calls,
not the complete attention transaction, AXI stalls, max/quantize or reciprocal.
"""

import argparse
import csv
import json
from pathlib import Path


OUT = Path(__file__).resolve().parent
D, H, M, PE_HOP, FEEDBACK, ACC_HOP = 4, 16, 8, 7, 8, 11


def write_csv(name, rows):
    with (OUT / name).open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def phases(n):
    return {
        "QK": (H-1)*max(n, FEEDBACK)+n+PE_HOP,
        "ALPHA_SCALE": n+ACC_HOP,
        "ALPHA_PWL": n+ACC_HOP,
        "SUB_MAX": n+PE_HOP,
        "SCALE": n+PE_HOP,
        "PWL": 8*n+PE_HOP,
        "ROW_SUM": n+D*PE_HOP+ACC_HOP,
        "PV": n*H+D*PE_HOP+ACC_HOP,
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("summary", type=Path)
    args = parser.parse_args()
    summary = json.loads(args.summary.read_text(encoding="utf-8-sig"))
    assert (summary["contexts"], summary["pe_hop"], summary["qk_feedback"], summary["acc_hop"]) == (M, PE_HOP, FEEDBACK, ACC_HOP)
    reports = summary["reports"]
    pipeline = next(v for k, v in reports.items() if "runSharedEngine_Pipeline" in k)
    assert pipeline["PipelineII"] == ["1"], "Cannot budget an II2 candidate as II1"
    module = reports["runSharedEngine_csynth.xml"]
    # LOOP_TRIPCOUNT is 1..(H*max(M, feedback)+D*hop+acc)=1..167.
    minimum = int(module["Best-caseLatency"][0])
    maximum = int(module["Worst-caseLatency"][0])
    assert maximum-minimum == H*M+D*PE_HOP+ACC_HOP-1
    overhead = minimum-1
    batches, events = [], []
    for n in range(1, M+1):
        p = phases(n)
        core = sum(p[k]+overhead for k in ("QK", "ROW_SUM", "PV"))
        batches.append({
            "contexts": n, **p, "engine_fixed_overhead_per_call": overhead,
            "core_QK_RS_L_PV_O": core,
            "ebe529b_core_same_queries": 201*n,
            "73110dc_core_same_queries": 163*n,
            "eight_compute_calls": sum(p.values())+8*overhead,
            "ebe529b_eight_component_calls": 235*n,
            "73110dc_eight_component_calls": 197*n,
            "normalize_source_cycles": n*H+ACC_HOP,
            "normalize_module_budget": n*H+ACC_HOP+overhead,
            "QK_real_vector_tokens": H*n,
            "QK_internal_holes": (H-1)*(max(n, FEEDBACK)-n),
            "PE_valid_token_evaluations": 688*n,
            "PE_unconditional_call_evaluations": sum(p.values())*D*D,
            "PE_QK_RS_PV_useful_MACs_full_unmasked": 528*n,
            "old_PE_evaluations": 1504*n,
        })
        for phase, cycles in p.items():
            launches = []
            if phase == "QK":
                launches = [(feature*max(n, FEEDBACK)+ctx, ctx, feature) for feature in range(H) for ctx in range(n)]
            elif phase == "PWL":
                launches = [(piece*n+ctx, ctx, piece) for piece in range(8) for ctx in range(n)]
            elif phase == "PV":
                launches = [(feature*n+ctx, ctx, feature) for feature in range(H) for ctx in range(n)]
            else:
                launches = [(ctx, ctx, 0) for ctx in range(n)]
            lookup = {cycle: (ctx, index) for cycle, ctx, index in launches}
            age = D*PE_HOP+ACC_HOP if phase in ("ROW_SUM", "PV") else (ACC_HOP if phase.startswith("ALPHA") else PE_HOP)
            for cycle in range(cycles):
                token = lookup.get(cycle)
                events.append({"contexts": n, "phase": phase, "source_iteration": cycle,
                               "real_token": int(token is not None),
                               "context": token[0] if token else "",
                               "feature_or_piece": token[1] if token else "",
                               "logical_commit_iteration": cycle+age if token else "",
                               "module_fixed_overhead_separate": overhead})
    totals = []
    for length in (3, 4, 5, 8, 9, 12, 16, 20, 24, 28, 32, 36, 64, 128):
        tiles = (length+D-1)//D
        for causal in (False, True):
            core = calls = holes = pairs = 0
            for base in range(0, tiles, M):
                count = min(M, tiles-base)
                for key in range(base+count if causal else tiles):
                    n = count-max(0, key-base) if causal else count
                    assert 0 < n <= M
                    p = phases(n)
                    pairs += n
                    core += sum(p[k]+overhead for k in ("QK", "ROW_SUM", "PV"))
                    calls += sum(p.values())+8*overhead
                    holes += (H-1)*(max(n, FEEDBACK)-n)
            expected_pairs = tiles*(tiles+1)//2 if causal else tiles*tiles
            assert pairs == expected_pairs
            totals.append({"N": length, "causal": int(causal), "query_tiles": tiles,
                           "legal_tile_pairs_including_padding": pairs,
                           "core_compute_call_budget": core,
                           "ebe529b_core_call_budget": pairs*201,
                           "73110dc_core_call_budget": pairs*163,
                           "eight_compute_call_budget": calls,
                           "ebe529b_eight_component_budget": pairs*235,
                           "73110dc_eight_component_budget": pairs*197,
                           "QK_internal_holes": holes,
                           "scope": "compute calls only; excludes controller, IO, reciprocal, normalization"})
    write_csv("accepted_phase_budgets.csv", batches)
    write_csv("accepted_cycle_tokens.csv", events)
    write_csv("accepted_sequence_compute_budgets.csv", totals)
    manifest = {"tested_commit": summary["commit"], "summary": args.summary.name,
                "scope": "HLS schedule prediction; not RTL or measured attention latency",
                "M": M, "pe_hop": PE_HOP, "feedback": FEEDBACK, "score_commit_distance": FEEDBACK-PE_HOP,
                "acc_hop": ACC_HOP, "module_fixed_overhead": overhead,
                "numeric_state_per_global_context_bytes": 576,
                "numeric_state_total_bytes": 576*M,
                "packed_flags_total_bytes": 5*M,
                "fixed_duplicated_KV_bytes": 512,
                "ring_source_payload_ceiling_bytes": 1495.75,
                "minimum_full_issue_contexts_for_this_verified_feedback": FEEDBACK}
    (OUT / "accepted_schedule_manifest.json").write_text(json.dumps(manifest, indent=2)+"\n", encoding="utf-8")
    print(json.dumps({"overhead": overhead, "full_batch": batches[-1], "short_batch": batches[0]}))


if __name__ == "__main__":
    main()
