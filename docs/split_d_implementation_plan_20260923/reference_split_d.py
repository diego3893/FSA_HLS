"""Executable specification checks, NOT an HLS implementation or timing model.

Revised 2026-09-23: PE reg reuse, seeded score chain, descending D,
independent BK, causal tile skipping, packed SRAM and result-token ledgers.
Uses Python float (FP64), true exp/exp2, optionally an FP16/FTZ P register.
Does not model FP32 FMA rounding, PWL, SRAM ports, FIFOs, or AXI timing.
All generated output stays beside this script. Requires only Python stdlib.
"""

import json
import math
import random
import struct
from dataclasses import dataclass
from pathlib import Path


@dataclass
class PE:
    reg: float = 0.0
    score_acc: float = 0.0


def half(value):
    return struct.unpack("<e", struct.pack("<e", value))[0]


def half_ftz(value):
    # Illustrative FTZ of the unrounded result; not the Raw FMA bit oracle.
    return math.copysign(0.0, value) if abs(value) < 2.0 ** -14 else half(value)


def ceildiv(n, d):
    return (n + d - 1) // d


def dense_reference(q, k, v, allowed):
    """Independent full-row reference, no KV or D tiling."""
    dim = len(q[0])
    out = []
    for i, qi in enumerate(q):
        keys = [j for j in range(len(k)) if allowed(i, j)]
        if not keys:
            out.append([0.0] * dim)
            continue
        scores = [math.fsum(a * b for a, b in zip(qi, k[j])) for j in keys]
        mx = max(scores)
        weights = [math.exp((s - mx) / math.sqrt(dim)) for s in scores]
        denom = math.fsum(weights)
        out.append([
            math.fsum(w * v[j][h] for w, j in zip(weights, keys)) / denom
            for h in range(dim)
        ])
    return out


def split_reference(q, k, v, rows, bq, engines, allowed,
                    p_half=False, bug=None, bk=None, causal_skip=False):
    """PE score mapping and macro-event order of the proposed design.

    Sequentially models independent engines; this is not hardware parallelism.
    """
    length, dim = len(q), len(q[0])
    bk = rows if bk is None else bk
    assert 1 <= bk <= rows and bq <= rows
    nd = ceildiv(dim, rows)
    output = [[None] * dim for _ in range(length)]
    counters = dict(score_writes=0, p_writes=0, l_updates=0, o_updates=0,
                    query_tiles=0, kv_requests=0, d_slices=0,
                    v_slices=0, p_preservation_checks=0, result_tokens=0,
                    qk_valid_macs=0)
    trace = []
    scale = math.log2(math.e) / math.sqrt(rows if bug == "scale_by_rows" else dim)
    for wave in range(0, length, engines * bq):
        for engine in range(engines):
            qb = wave + engine * bq
            if qb >= length:
                continue
            aq = min(bq, length - qb)
            counters["query_tiles"] += 1
            mesh = [[PE() for _ in range(bq)] for _ in range(rows)]
            m = [-math.inf] * bq
            ell = [0.0] * bq
            o = [[0.0] * dim for _ in range(bq)]
            history = [False] * bq
            key_limit = min(length, ceildiv(qb + aq, bk) * bk) if causal_skip else length
            for kb in range(0, key_limit, bk):
                ak = min(bk, length - kb)
                counters["kv_requests"] += 1
                for row in mesh:
                    for pe in row:
                        pe.score_acc = pe.reg = 0.0

                # Different physical roles: r=feature for the MAC,
                # destination row=key for score storage.
                for t in reversed(range(nd)):
                    counters["d_slices"] += 1
                    if bug == "clear_each_d":
                        for row in mesh:
                            for pe in row:
                                pe.score_acc = 0.0
                    for r in range(rows):
                        g = t * rows + r
                        for col in range(aq):
                            mesh[r][col].reg = q[qb + col][g] if g < dim else 0.0
                    for key_lane in range(ak):
                        for col in range(aq):
                            old = mesh[key_lane][col].score_acc
                            partial = old
                            for r in reversed(range(rows)):
                                g = t * rows + r
                                if g < dim:
                                    partial += mesh[r][col].reg * k[kb + key_lane][g]
                                    counters["qk_valid_macs"] += 1
                            if bug == "double_seed":
                                partial += old
                            mesh[key_lane][col].score_acc = partial
                            counters["score_writes"] += 1

                alpha = [1.0] * bq
                tile_has = [False] * bq
                for col in range(aq):
                    valid_keys = [r for r in range(ak) if allowed(qb + col, kb + r)]
                    tile_has[col] = bool(valid_keys)
                    if valid_keys:
                        tile_max = max(mesh[r][col].score_acc for r in valid_keys)
                        new_m = max(m[col], tile_max) if history[col] else tile_max
                        alpha[col] = 2.0 ** ((m[col] - new_m) * scale) if history[col] else 0.0
                        if bug == "omit_alpha" and history[col]:
                            alpha[col] = 1.0
                        for r in range(rows):
                            p = (2.0 ** ((mesh[r][col].score_acc - new_m) * scale)
                                 if r in valid_keys else 0.0)
                            mesh[r][col].reg = half_ftz(p) if p_half else p
                        # Same stored P feeds L and PV.
                        row_sum = sum(mesh[r][col].reg for r in range(rows))
                        if bug == "rowsum_per_v_tile":
                            row_sum *= nd
                        ell[col] = alpha[col] * ell[col] + row_sum
                        m[col] = new_m
                        history[col] = True
                    else:
                        for r in range(rows):
                            mesh[r][col].reg = 0.0
                    counters["p_writes"] += rows
                    counters["l_updates"] += 1  # includes an explicit no-op for masked rows

                p_snapshot = [[pe.reg for pe in row] for row in mesh]
                emitted = ["MAX_DIFF", "ROW_SUM"]
                for vt in range(nd):
                    counters["v_slices"] += 1
                    if bug == "overwrite_p" and vt:
                        for row in mesh:
                            for pe in row:
                                pe.reg = 0.0
                    if bug == "rescale_entire_o_each_v":
                        for col in range(aq):
                            for h in range(dim):
                                o[col][h] *= alpha[col]
                    for u in range(rows):
                        h = vt * rows + u
                        if h >= dim:
                            continue
                        emitted.append(h)
                        for col in range(aq):
                            pv = sum(mesh[r][col].reg * v[kb + r][h] for r in range(ak))
                            if bug == "rescale_entire_o_each_v":
                                o[col][h] += pv
                            else:
                                o[col][h] = alpha[col] * o[col][h] + pv
                            counters["o_updates"] += 1
                    if bug != "overwrite_p":
                        assert p_snapshot == [[pe.reg for pe in row] for row in mesh]
                    counters["p_preservation_checks"] += 1
                assert emitted == ["MAX_DIFF", "ROW_SUM"] + list(range(dim))
                counters["result_tokens"] += len(emitted)
                if qb == 0:
                    trace.append(dict(key_base=kb, m=m[0] if history[0] else None,
                                      l=ell[0], alpha=alpha[0], tile_has=tile_has[0]))

            for col in range(aq):
                for h in range(dim):
                    val = o[col][h] / ell[col] if ell[col] > 0.0 else 0.0
                    if bug == "normalize_only_rows" and h >= rows:
                        val = o[col][h]
                    assert output[qb + col][h] is None, "duplicate output owner"
                    output[qb + col][h] = val
    assert all(x is not None for row in output for x in row), "missing output"
    return output, counters, trace


def max_error(a, b):
    return max(abs(x - y) for ra, rb in zip(a, b) for x, y in zip(ra, rb))


def matrices(length, dim, seed):
    rng = random.Random(seed)
    return tuple([[half(rng.uniform(-1, 1)) for _ in range(dim)]
                  for _ in range(length)] for _ in range(3))


def check_layout():
    details = []
    for dim in (1, 4, 5, 10, 17, 128):
        for width, fmt in ((16, "e"), (32, "f")):
            lanes = 64 // width
            stride = ceildiv(dim, lanes)
            values = [[float(100 * t + h) / 8.0 for h in range(dim)] for t in range(3)]
            words = [0] * (3 * stride)
            for token in range(3):
                for feature in range(dim):
                    bits = int.from_bytes(struct.pack("<" + fmt, values[token][feature]), "little")
                    addr = token * stride + feature // lanes
                    words[addr] |= bits << ((feature % lanes) * width)
            for token in range(3):
                for lane_index in range(stride * lanes):
                    addr = token * stride + lane_index // lanes
                    bits = (words[addr] >> ((lane_index % lanes) * width)) & ((1 << width) - 1)
                    restored = struct.unpack("<" + fmt, bits.to_bytes(width // 8, "little"))[0]
                    expected = values[token][lane_index] if lane_index < dim else 0.0
                    assert restored == expected
            details.append(dict(dim=dim, element_width=width, words_per_row=stride))
    return details


def check_broadcast():
    """Abstract pending-mask delivery check, not a FIFO/RTL model."""
    rng = random.Random(500)
    received = [[] for _ in range(4)]
    expected = [[] for _ in range(4)]
    for seq in range(80):
        targets = [i for i in range(4) if (i + seq) % 3]
        for i in targets:
            expected[i].append(seq)
        pending = set(targets)
        for tick in range(200):
            for i in list(pending):
                if tick > 100 or rng.random() < 0.3:
                    received[i].append(seq)
                    pending.remove(i)
            if not pending:
                break
        assert not pending
    assert received == expected
    return dict(packets=80, consumers=4, result="PASS", limitation="abstract handshake only")


def traffic():
    length, dim, bq, bk, engines = 4096, 128, 16, 16, 8
    nq, nk = ceildiv(length, bq), ceildiv(length, bk)
    waves = ceildiv(nq, engines)
    old = 2 * length * dim * nk + 4 * length * dim * nq + 4 * length * dim
    cache = 2 * length * dim + 4 * length * dim * nq + 4 * length * dim
    shared = 2 * length * dim + 4 * length * dim * waves + 4 * length * dim
    region = 2 * length * dim + 4 * length * dim * waves * 3 + 4 * length * dim
    mib = 1024 ** 2
    assert (old // mib, cache // mib, shared // mib, region // mib) == (770, 515, 67, 195)
    return dict(historical_no_q_cache_bytes=old, current_stream_baseline_bytes=cache, shared_wave_bytes=shared,
                three_regions_per_wave_bytes=region, measured_hardware=False)


def check_spad_layout():
    results = []
    for dim, rows, bq, bk in ((1,4,2,3),(5,4,2,3),(17,8,4,8),(128,16,16,16)):
        nd, subbanks = ceildiv(dim, rows), rows // 4
        base, used = 0, set()
        for name, lanes in (("Q0",bq),("Q1",bq),("K0",bk),("K1",bk),("V0",bk),("V1",bk)):
            physical = {}
            for t in range(nd):
                for lane in range(lanes):
                    address = base + t*lanes + lane
                    assert address not in used
                    used.add(address)
                    for r in range(rows):
                        feature = t*rows+r
                        physical[(address,r//4,r%4)] = (lane,feature) if feature < dim else None
            # Enumerate external row-major DMA words and invert their local destination.
            for lane in range(lanes):
                for word in range(ceildiv(dim,4)):
                    g = word*4
                    address = base+(g//rows)*lanes+lane
                    subbank = (g%rows)//4
                    assert 0 <= subbank < subbanks
                    for within in range(4):
                        expected = (lane,g+within) if g+within < dim else None
                        assert physical[(address,subbank,within)] == expected
            base += nd*lanes
        assert used == set(range(nd*(2*bq+4*bk)))
        assert base == len(used)
        results.append(dict(dim=dim,rows=rows,bq=bq,bk=bk,spad_rows=base,
                            spad_bytes=base*rows*2,acc_bytes=(dim+1)*bq*4,result="PASS"))
    return results


def check_causal_traffic():
    results = []
    for length, dim, bq, bk, engines in ((11,9,2,3,4),(13,17,4,8,2),(35,12,4,4,8)):
        for causal in (False,True):
            bases = list(range(0,length,bq))
            expected_prefix = lambda qb: (min(length,ceildiv(qb+min(bq,length-qb),bk)*bk)
                                          if causal else length)
            explicit_sets = []
            for qb in bases:
                # Independently choose each tile by testing existence of an allowed pair.
                keys = set()
                for kb in range(0,length,bk):
                    ak = min(bk,length-kb)
                    needed = any(not causal or j <= i for i in range(qb,min(qb+bq,length))
                                 for j in range(kb,kb+ak))
                    if needed:
                        keys.update(range(kb,kb+ak))
                assert keys == set(range(expected_prefix(qb)))
                explicit_sets.append(keys)
            independent_rows = sum(map(len,explicit_sets))
            shared_rows = 0
            for start in range(0,len(bases),engines):
                group = explicit_sets[start:start+engines]
                union = set().union(*group)
                assert len(union) == max(expected_prefix(qb) for qb in bases[start:start+engines])
                shared_rows += len(union)
            in_bytes, out_bytes = 8*ceildiv(dim,4),8*ceildiv(dim,2)
            fixed = length*(in_bytes+out_bytes)
            results.append(dict(length=length,dim=dim,bq=bq,bk=bk,engines=engines,causal=causal,
                                independent_bytes=fixed+2*in_bytes*independent_rows,
                                shared_bytes=fixed+2*in_bytes*shared_rows,result="PASS"))
    return results


def check_ftz():
    assert half(2.0**-15) != 0.0
    assert half_ftz(2.0**-15) == 0.0
    assert half_ftz(2.0**-14) == 2.0**-14
    assert math.copysign(1.0,half_ftz(-2.0**-15)) == -1.0
    return dict(result="PASS",scope="FTZ boundary illustration only; CMP cast is not emulated")


def run():
    results = []
    # Covers D/query/key tails, a wave with inactive engines, and different R.
    configurations = [(1, 1, 4, 4, 4), (5, 5, 4, 4, 4), (9, 12, 4, 4, 4),
                      (11, 17, 4, 4, 4), (17, 128, 16, 16, 16), (19, 128, 8, 8, 8),
                      (35, 12, 4, 4, 4), (13, 17, 8, 4, 8), (11, 9, 4, 2, 3)]
    for length, dim, rows, bq, bk in configurations:
        q, k, v = matrices(length, dim, seed=length * 1000 + dim)
        for causal in (False, True):
            allowed = (lambda i, j: j <= i) if causal else (lambda i, j: True)
            golden = dense_reference(q, k, v, allowed)
            for engines in (1, 2, 4, 8):
                got, count, _ = split_reference(q, k, v, rows, bq, engines, allowed,
                                                bk=bk, causal_skip=causal)
                err = max_error(got, golden)
                assert err < 1e-11, (length, dim, causal, engines, err)
                nq = ceildiv(length, bq)
                requests = sum(ceildiv(qb + min(bq, length-qb), bk) if causal
                               else ceildiv(length, bk) for qb in range(0, length, bq))
                query_key_pairs = sum(min(bq, length-qb) *
                    (min(length, ceildiv(qb+min(bq,length-qb),bk)*bk) if causal else length)
                    for qb in range(0,length,bq))
                query_tile_visits = sum(min(bq,length-qb) *
                    (ceildiv(qb+min(bq,length-qb),bk) if causal else ceildiv(length,bk))
                    for qb in range(0,length,bq))
                nd = ceildiv(dim, rows)
                assert count["query_tiles"] == nq
                assert count["kv_requests"] == requests
                assert count["d_slices"] == count["v_slices"] == requests * nd
                assert count["score_writes"] == query_key_pairs * nd
                assert count["qk_valid_macs"] == query_key_pairs * dim
                assert count["p_writes"] == query_tile_visits * rows
                assert count["l_updates"] == query_tile_visits
                assert count["o_updates"] == query_tile_visits * dim
                assert count["result_tokens"] == requests * (dim+2)
                results.append(dict(length=length, dim=dim, rows=rows, bq=bq,
                                    bk=bk, engines=engines, causal=causal, max_abs_error=err,
                                    ledger=count,
                                    result="PASS"))

    # No history + masked tile, later first valid tile, then all-masked again.
    q, k, v = matrices(13, 10, 53)
    allowed = lambda i, j: i != 2 and 4 <= j < 8
    masked_golden = dense_reference(q, k, v, allowed)
    got, _, masked_trace = split_reference(q, k, v, 4, 4, 2, allowed)
    assert max_error(got, masked_golden) < 1e-11
    assert got[2] == [0.0] * 10
    assert masked_trace[0]["m"] is None and masked_trace[0]["l"] == 0
    assert masked_trace[1]["alpha"] == 0
    assert masked_trace[2]["alpha"] == 1

    # Large max increase activates alpha rescaling. Data are exactly FP16 representable.
    q, k, v = matrices(9, 12, 174)
    q = [[0.5 + (h % 3) * 0.125 for h in range(12)] for _ in range(9)]
    k = [[(-0.5 if j < 4 else 0.25 if j < 8 else 1.0) + (h % 2) * 0.125
          for h in range(12)] for j in range(9)]
    allowed = lambda i, j: True
    golden = dense_reference(q, k, v, allowed)
    got, _, alpha_trace = split_reference(q, k, v, 4, 4, 1, allowed)
    assert max_error(got, golden) < 1e-11
    assert all(0 < row["alpha"] < 1 for row in alpha_trace[1:])
    mutation_results = []
    for bug in ("scale_by_rows", "clear_each_d", "double_seed", "omit_alpha", "rowsum_per_v_tile",
                "overwrite_p", "rescale_entire_o_each_v", "normalize_only_rows"):
        bad, _, _ = split_reference(q, k, v, 4, 4, 1, allowed, bug=bug)
        err = max_error(bad, golden)
        assert err > 1e-5, ("test did not detect mutation", bug, err)
        mutation_results.append(dict(mutation=bug, detected=True, max_abs_error=err))

    q, k, v = matrices(19, 128, 993)
    allowed = lambda i, j: j <= i
    p16, _, _ = split_reference(q, k, v, 8, 8, 4, allowed, p_half=True)
    p16_error = max_error(p16, dense_reference(q, k, v, allowed))
    assert p16_error < 5e-3  # Only this bounded sample; not an HLS error budget.
    return dict(
        scope="FP64 true-exp seeded-chain specification; optional stored P FP16/FTZ; no HLS/RTL/PWL/FP32 proof",
        result="PASS", standard_cases=results,
        all_masked_and_late_history=dict(result="PASS", trace=masked_trace),
        alpha_change=dict(result="PASS", trace=alpha_trace),
        mutation_checks=mutation_results,
        p_fp16_sample=dict(result="PASS", max_abs_error=p16_error,
                           caveat="FP64 alpha and products, no FP32/PWL emulation"),
        packed_layout_cases=check_layout(), broadcast_check=check_broadcast(),
        spad_layout_cases=check_spad_layout(), causal_traffic_cases=check_causal_traffic(),
        ftz_boundary_check=check_ftz(), traffic_formula_check=traffic())


if __name__ == "__main__":
    report = run()
    destination = Path(__file__).resolve().parent / "reference_results.json"
    destination.write_text(json.dumps(report, indent=2, ensure_ascii=False, allow_nan=False) + "\n",
                           encoding="utf-8")
    print("PASS: {} standard cases, {} detected mutations, masks/alpha/layout/broadcast/traffic".format(
        len(report["standard_cases"]), len(report["mutation_checks"])))
    print("Saved:", destination)
