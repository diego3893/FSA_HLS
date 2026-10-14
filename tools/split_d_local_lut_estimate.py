"""Diagnostic LUT/context sweep from archived Vitis reports.

This is an extrapolation, not a replacement for CSynth. The M1/M8 anchor
commits also differ in L_MAX, storage directives and initialization, so the slope
is deliberately labeled as confounded.
"""

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
EVIDENCE = ROOT / "docs/evidence"
M1_PATH = EVIDENCE / "split_d_stage4_20261012/round1_6f0b40d_M1_summary.json"
M8_PATH = EVIDENCE / "split_d_stage4_20261013/stage4_resume_0bfd6d2_summary.json"
LATEST_PATH = EVIDENCE / "split_d_stage4_20261013/stage4_resume_d53df16_summary.json"
OLD_PATH = EVIDENCE / "split_d_ebe529b_16x16_timing/summary.json"
OUT_PATH = EVIDENCE / "split_d_stage4_20261013/local_lut_context_sweep.json"


def load(path, prefix, contexts, length_max):
    data = json.loads(path.read_text(encoding="utf-8"))
    assert data["commit"].startswith(prefix)
    assert data["contexts"] == contexts
    assert (data["D"], data["H"], data["L_MAX"]) == (16, 128, length_max)
    return data


def lut(report, name):
    return int(report["reports"][name]["LUT"][0])


def later_key_cycles(contexts, overhead, feedback, hop, acc_hop):
    """Eight shared-engine calls only; excludes AXI and controller cycles."""
    gap = max(contexts, feedback)
    qk = 127 * gap + contexts + hop + overhead
    alpha = contexts + acc_hop + overhead
    sub_or_scale = contexts + hop + overhead
    pwl = 8 * contexts + hop + overhead
    row_sum = contexts + 16 * hop + acc_hop + overhead
    pv = 128 * contexts + 16 * hop + acc_hop + overhead
    return qk + 2 * alpha + 2 * sub_or_scale + pwl + row_sum + pv


def legacy_tile_cycles():
    data = json.loads(OLD_PATH.read_text(encoding="utf-8"))["report_summary"]

    def cycles(name):
        return int(data[name]["overall"]["Best-caseLatency"])

    phase_suffixes = (
        "VITIS_LOOP_109_1_VITIS_LOOP_110_2",
        "VITIS_LOOP_162_1",
        "VITIS_LOOP_300_25",
        "VITIS_LOOP_369_33",
    )
    return (sum(cycles("runQueryBlock_0_Pipeline_" + suffix) for suffix in phase_suffixes)
            + 2 * cycles("runPeArray") + 3 * cycles("runAccumulatorColumns"))


def main():
    m1 = load(M1_PATH, "6f0b40d", 1, 4096)
    m8 = load(M8_PATH, "0bfd6d2", 8, 1024)
    latest = load(LATEST_PATH, "d53df16", 8, 1024)
    capacity = int(latest["reports"]["csynth.xml"]["LUT"][1])
    observed_delta = lut(m8, "csynth.xml") - lut(m1, "csynth.xml")
    assert observed_delta == 261980
    assert lut(latest, "csynth.xml") == 1531724
    assert lut(m1, "runPeArray_csynth.xml") == lut(m8, "runPeArray_csynth.xml")
    overhead = int(latest["reports"]["runSharedEngine_csynth.xml"]["Best-caseLatency"][0]) - 1
    feedback, hop, acc_hop = (latest[key] for key in ("qk_feedback", "pe_hop", "acc_hop"))
    reference_cycles = later_key_cycles(8, overhead, feedback, hop, acc_hop) / 8
    old_tile_cycles = legacy_tile_cycles()
    assert old_tile_cycles == 12427

    # One 54-bit lshr per PE at 161 operator-LUT, across 256 PEs.
    # The new pre-sort may add logic; this is only a gross eliminated-operator
    # illustration and is not a bound on top-level synthesis savings.
    gross_shift_operator_lut = 256 * 161
    # d53df16 archived runPeArray_csynth.rpt SHA256:
    # d70aa73c5b4fb08a9970c7dc59dd297c91b0662dce6d212e6cca56a590110d8f
    # Three source-level normalization shifts and magnitude add/sub,
    # summed across the two workers. These are expression rows, not net LUT.
    gross_normalization_operator_lut = 2 * (9216 + 9216 + 8960)
    gross_magnitude_operator_lut = 2 * (4480 + 8960)
    gross_selected_expression_lut = (gross_shift_operator_lut
                                     + gross_normalization_operator_lut
                                     + gross_magnitude_operator_lut)
    rows = []
    for contexts in range(1, 9):
        estimate = round(lut(latest, "csynth.xml") - (8 - contexts) * observed_delta / 7)
        cycles_per_query_tile = later_key_cycles(contexts, overhead, feedback, hop, acc_hop) / contexts
        rows.append({
            "contexts": contexts,
            "qk_issue_gap_cycles": max(contexts, feedback),
            "gross_qk_internal_holes_per_key": 127 * max(0, feedback - contexts),
            "diagnostic_lut": estimate,
            "diagnostic_headroom_lut": capacity - estimate,
            "gross_shift_operator_credit_lut": gross_shift_operator_lut,
            "diagnostic_lut_if_full_gross_credit": estimate - gross_shift_operator_lut,
            "later_key_shared_cycles_per_query_tile": round(cycles_per_query_tile, 3),
            "later_key_shared_cycles_ratio_to_m8": round(cycles_per_query_tile / reference_cycles, 3),
            "later_key_shared_cycles_ratio_to_legacy": round(cycles_per_query_tile / old_tile_cycles, 3),
        })

    result = {
        "scope": "Local diagnostic; no CSynth was run on the candidate or intermediate M values",
        "anchors": {
            "m1_commit": m1["commit"],
            "m1_lut": lut(m1, "csynth.xml"),
            "m1_l_max": m1["L_MAX"],
            "m1_bram18": int(m1["reports"]["csynth.xml"]["BRAM_18K"][0]),
            "m8_commit": m8["commit"],
            "m8_lut": lut(m8, "csynth.xml"),
            "m8_l_max": m8["L_MAX"],
            "m8_bram18": int(m8["reports"]["csynth.xml"]["BRAM_18K"][0]),
            "latest_m8_commit": latest["commit"],
            "latest_m8_lut": lut(latest, "csynth.xml"),
            "capacity_lut": capacity,
            "observed_m1_to_m8_delta_lut": observed_delta,
            "observed_delta_per_context_lut": observed_delta / 7,
            "observed_worker0_delta_lut": (lut(m8, "runQueryBlock_0_s_csynth.xml")
                                           - lut(m1, "runQueryBlock_0_s_csynth.xml")),
            "observed_shared_engine_delta_per_worker_lut": (lut(m8, "runSharedEngine_csynth.xml")
                                                            - lut(m1, "runSharedEngine_csynth.xml")),
            "observed_pe_array_delta_per_worker_lut": (lut(m8, "runPeArray_csynth.xml")
                                                       - lut(m1, "runPeArray_csynth.xml")),
            "legacy_tile_shared_cycles": old_tile_cycles,
            "gross_selected_expression_lut": gross_selected_expression_lut,
            "gross_selected_expression_components": {
                "one_alignment_shift": gross_shift_operator_lut,
                "three_normalization_shifts": gross_normalization_operator_lut,
                "historical_magnitude_add_sub": gross_magnitude_operator_lut,
            },
            "latest_m8_lut_if_selected_expression_rows_vanished": (
                lut(latest, "csynth.xml") - gross_selected_expression_lut),
            "latest_m8_gap_if_selected_expression_rows_vanished": (
                lut(latest, "csynth.xml") - gross_selected_expression_lut - capacity),
        },
        "limitations": [
            "M1 and M8 anchor commits also differ in L_MAX, BRAM binding and state initialization; context slope is confounded.",
            "Linear extrapolation ignores width thresholds, bank changes, logic sharing and placement effects.",
            "The PE source candidate has no CSynth result. Gross eliminated shift-operator LUT is not a top-level saving or a rigorous bound.",
            "The selected-expression accounting imagines their reported LUT rows disappear for free; it does not predict actual DSP mapping or top-level LUT.",
            "Cycle ratios cover eight shared-compute calls for a later key tile only; they exclude AXI, controller, first-key and full transaction latency.",
            "Legacy shared-compute reference is a different L_MAX=4096 design and is not a full Attention latency comparison.",
            "The accepted design must independently pass official bitwise CSim, II, timing, RTL-instance, BRAM and resource gates.",
        ],
        "rows": rows,
    }
    OUT_PATH.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print("M  LUT-diagnostic  headroom  per-query-tile-cycles  ratio-to-M8")
    for row in rows:
        print(f"{row['contexts']:<2} {row['diagnostic_lut']:>14} {row['diagnostic_headroom_lut']:>9} "
              f"{row['later_key_shared_cycles_per_query_tile']:>21.3f} "
              f"{row['later_key_shared_cycles_ratio_to_m8']:>11.3f}")
    print(f"Wrote {OUT_PATH}")


if __name__ == "__main__":
    main()
