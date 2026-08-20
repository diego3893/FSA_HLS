#!/usr/bin/env python3
"""Read-only inventory and summary of a Vitis HLS build solution."""

from __future__ import annotations

import argparse
import json
import re
import sys
from collections import Counter
from datetime import datetime
from pathlib import Path
from typing import Any, Iterable
import xml.etree.ElementTree as ET


def timestamp(path: Path | None) -> str | None:
    if path is None or not path.exists():
        return None
    return datetime.fromtimestamp(path.stat().st_mtime).astimezone().isoformat(timespec="seconds")


def latest_timestamp(paths: Iterable[Path]) -> float:
    values = [path.stat().st_mtime for path in paths if path.exists()]
    return max(values) if values else 0.0


def first_file(paths: Iterable[Path]) -> Path | None:
    files = sorted((path for path in paths if path.is_file()), key=lambda path: path.name)
    return files[0] if files else None


def read_text(path: Path | None) -> str:
    if path is None or not path.exists():
        return ""
    return path.read_text(encoding="utf-8", errors="replace")


def xml_text(root: ET.Element, path: str) -> str | None:
    value = root.findtext(path)
    if value is None:
        return None
    value = value.strip()
    return value or None


def number(value: str | None) -> int | float | str | None:
    if value is None:
        return None
    cleaned = value.strip()
    try:
        if re.fullmatch(r"[-+]?\d+", cleaned):
            return int(cleaned)
        return float(cleaned)
    except ValueError:
        return cleaned


def relative_or_absolute(path: Path | None, base: Path) -> str | None:
    if path is None:
        return None
    try:
        return str(path.relative_to(base))
    except ValueError:
        return str(path)


def top_xmls_under(directory: Path) -> list[tuple[Path, str]]:
    results: list[tuple[Path, str]] = []
    # syn/report/csynth.xml is the solution-level summary. Individual
    # <function>_csynth.xml files also contain TopModelName, but for that
    # function rather than for the solution top, so they must not drive
    # solution selection.
    for path in directory.rglob("csynth.xml"):
        if path.parent.name != "report" or path.parent.parent.name != "syn":
            continue
        try:
            root = ET.parse(path).getroot()
        except ET.ParseError:
            continue
        top = xml_text(root, "./UserAssignments/TopModelName")
        if top:
            results.append((path, top))
    return results


def solution_artifacts(solution: Path, top: str | None) -> dict[str, Path | None]:
    syn_report = solution / "syn" / "report"
    top_xml = syn_report / f"{top}_csynth.xml" if top else None
    top_rpt = syn_report / f"{top}_csynth.rpt" if top else None
    csim_dir = solution / "csim" / "report"
    csim = csim_dir / f"{top}_csim.log" if top and (csim_dir / f"{top}_csim.log").exists() else first_file(csim_dir.glob("*_csim.log")) if csim_dir.exists() else None
    cosim_dir = solution / "sim" / "report"
    cosim = cosim_dir / f"{top}_cosim.rpt" if top and (cosim_dir / f"{top}_cosim.rpt").exists() else first_file(cosim_dir.glob("*_cosim.rpt")) if cosim_dir.exists() else None
    transaction = first_file((solution / "sim" / "report").rglob("result.transaction.rpt")) if (solution / "sim" / "report").exists() else None
    component = solution / "impl" / "ip" / "component.xml"
    export_zip = solution / "impl" / "export.zip"
    solution_log = solution / f"{solution.name}.log"
    if not solution_log.exists():
        solution_log = first_file(solution.glob("*.log"))
    solution_data = solution / "solution1_data.json"
    if not solution_data.exists():
        solution_data = first_file(solution.glob("*_data.json"))
    return {
        "top_xml": top_xml if top_xml and top_xml.exists() else None,
        "top_rpt": top_rpt if top_rpt and top_rpt.exists() else None,
        "csim_log": csim,
        "cosim_report": cosim,
        "transaction_report": transaction,
        "component_xml": component if component.exists() else None,
        "export_zip": export_zip if export_zip.exists() else None,
        "solution_log": solution_log,
        "solution_data": solution_data,
    }


def candidate_score(artifacts: dict[str, Path | None]) -> int:
    weights = {
        "top_xml": 5,
        "top_rpt": 2,
        "csim_log": 1,
        "cosim_report": 3,
        "transaction_report": 1,
        "component_xml": 2,
        "export_zip": 2,
        "solution_log": 1,
        "solution_data": 1,
    }
    return sum(weights[name] for name, path in artifacts.items() if path is not None)


def discover(directory: Path) -> list[dict[str, Any]]:
    by_solution: dict[Path, str] = {}
    for xml_path, top in top_xmls_under(directory):
        by_solution[xml_path.parent.parent.parent] = top

    if not by_solution:
        probes = [directory]
        if directory.is_file():
            probes = list(directory.parents)
        else:
            probes.extend(directory.parents)
        for probe in probes:
            if (probe / "syn" / "report").is_dir():
                summary_xml = probe / "syn" / "report" / "csynth.xml"
                if summary_xml.exists():
                    try:
                        summary_root = ET.parse(summary_xml).getroot()
                        by_solution[probe] = xml_text(summary_root, "./UserAssignments/TopModelName") or ""
                    except ET.ParseError:
                        by_solution[probe] = ""
                else:
                    by_solution[probe] = ""
                break

    candidates: list[dict[str, Any]] = []
    for solution, top in by_solution.items():
        if not top:
            xml_file = first_file((solution / "syn" / "report").glob("*_csynth.xml"))
            if xml_file:
                try:
                    top = xml_text(ET.parse(xml_file).getroot(), "./UserAssignments/TopModelName") or ""
                except ET.ParseError:
                    top = ""
        artifacts = solution_artifacts(solution, top or None)
        paths = [path for path in artifacts.values() if path is not None]
        candidates.append(
            {
                "solution": solution,
                "top": top or None,
                "artifacts": artifacts,
                "score": candidate_score(artifacts),
                "latest_epoch": latest_timestamp(paths),
            }
        )
    candidates.sort(key=lambda item: (item["score"], item["latest_epoch"]), reverse=True)
    return candidates


def parse_top_xml(path: Path) -> dict[str, Any]:
    root = ET.parse(path).getroot()
    assignments = {
        "tool_version": xml_text(root, "./ReportVersion/Version"),
        "top": xml_text(root, "./UserAssignments/TopModelName"),
        "product_family": xml_text(root, "./UserAssignments/ProductFamily"),
        "part": xml_text(root, "./UserAssignments/Part"),
        "flow_target": xml_text(root, "./UserAssignments/FlowTarget"),
        "target_period_ns": number(xml_text(root, "./UserAssignments/TargetClockPeriod")),
        "clock_uncertainty_ns": number(xml_text(root, "./UserAssignments/ClockUncertainty")),
    }
    performance = {
        "pipeline_type": xml_text(root, "./PerformanceEstimates/PipelineType"),
        "estimated_period_ns": number(xml_text(root, "./PerformanceEstimates/SummaryOfTimingAnalysis/EstimatedClockPeriod")),
        "latency_min_cycles": number(xml_text(root, "./PerformanceEstimates/SummaryOfOverallLatency/Best-caseLatency")),
        "latency_avg_cycles": number(xml_text(root, "./PerformanceEstimates/SummaryOfOverallLatency/Average-caseLatency")),
        "latency_max_cycles": number(xml_text(root, "./PerformanceEstimates/SummaryOfOverallLatency/Worst-caseLatency")),
        "interval_min_cycles": number(xml_text(root, "./PerformanceEstimates/SummaryOfOverallLatency/Interval-min")),
        "interval_max_cycles": number(xml_text(root, "./PerformanceEstimates/SummaryOfOverallLatency/Interval-max")),
    }

    target = assignments["target_period_ns"]
    uncertainty = assignments["clock_uncertainty_ns"]
    estimated = performance["estimated_period_ns"]
    if all(isinstance(value, (int, float)) for value in (target, uncertainty, estimated)):
        effective = float(target) - float(uncertainty)
        performance["derived_effective_budget_ns"] = round(effective, 6)
        performance["derived_timing_margin_ns"] = round(effective - float(estimated), 6)
        performance["derived_fmax_mhz"] = round(1000.0 / float(estimated), 6) if float(estimated) else None

    resources: dict[str, Any] = {}
    available: dict[str, Any] = {}
    percentages: dict[str, Any] = {}
    for name in ("BRAM_18K", "DSP", "FF", "LUT", "URAM"):
        used_value = number(xml_text(root, f"./AreaEstimates/Resources/{name}"))
        available_value = number(xml_text(root, f"./AreaEstimates/AvailableResources/{name}"))
        resources[name] = used_value
        available[name] = available_value
        if isinstance(used_value, (int, float)) and isinstance(available_value, (int, float)) and available_value:
            percentages[name] = round(float(used_value) / float(available_value) * 100.0, 6)
        else:
            percentages[name] = None

    ports: list[dict[str, Any]] = []
    for port in root.findall("./InterfaceSummary/RtlPorts"):
        ports.append(
            {
                "name": xml_text(port, "./name"),
                "direction": xml_text(port, "./Dir"),
                "bits": number(xml_text(port, "./Bits")),
                "protocol": xml_text(port, "./IOProtocol"),
                "source_object": xml_text(port, "./Object"),
                "c_type": xml_text(port, "./CType") or xml_text(port, "./Type"),
            }
        )

    return {
        "assignments": assignments,
        "performance": performance,
        "resources": resources,
        "available_resources": available,
        "derived_resource_percent": percentages,
        "interface_ports": ports,
    }


def parse_csim(path: Path | None) -> dict[str, Any]:
    text = read_text(path)
    pass_lines = [line.strip() for line in text.splitlines() if "[PASS]" in line]
    fail_lines = [line.strip() for line in text.splitlines() if "[FAIL]" in line]
    zero_errors = bool(re.search(r"CSim done with 0 errors", text, re.IGNORECASE))
    status = "fail" if fail_lines else "pass" if zero_errors else "unknown"
    return {"status": status, "pass_markers": pass_lines, "fail_markers": fail_lines}


def parse_cosim(path: Path | None) -> dict[str, Any]:
    text = read_text(path)
    result: dict[str, Any] = {"status": "not_found" if not text else "unknown"}
    for line in text.splitlines():
        if not re.match(r"^\|\s*(Verilog|VHDL)\s*\|", line):
            continue
        values = [part.strip() for part in line.strip().strip("|").split("|")]
        if len(values) < 9:
            continue
        rtl, status, lat_min, lat_avg, lat_max, int_min, int_avg, int_max, total = values[:9]
        if rtl == "Verilog" or result.get("rtl") is None:
            result = {
                "rtl": rtl,
                "status": status.lower(),
                "latency_min_cycles": number(lat_min),
                "latency_avg_cycles": number(lat_avg),
                "latency_max_cycles": number(lat_max),
                "interval_min_cycles": number(int_min),
                "interval_avg_cycles": number(int_avg),
                "interval_max_cycles": number(int_max),
                "total_execution_cycles": number(total),
            }
    return result


def parse_log(path: Path | None) -> dict[str, Any]:
    text = read_text(path)
    warnings: Counter[str] = Counter()
    warning_examples: dict[str, str] = {}
    errors: list[str] = []
    for line in text.splitlines():
        match = re.match(r"^(WARNING|ERROR):\s*\[([^\]]+)\]\s*(.*)$", line)
        if not match:
            continue
        level, code, message = match.groups()
        if level == "WARNING":
            warnings[code] += 1
            warning_examples.setdefault(code, message.strip())
        else:
            errors.append(f"[{code}] {message.strip()}")
    return {
        "warning_counts": dict(sorted(warnings.items())),
        "warning_examples": warning_examples,
        "error_count": len(errors),
        "error_examples": errors[:10],
        "flow_markers": {
            "cosim_pass": "C/RTL co-simulation finished: PASS" in text,
            "cosim_fail": "C/RTL co-simulation finished: FAIL" in text,
            "export_finished": "Finished Command export_design" in text,
            "timing_constraint_warning": "HLS 200-871" in text,
        },
    }


def parse_solution_data(path: Path | None) -> dict[str, Any]:
    if path is None or not path.exists():
        return {}
    try:
        data = json.loads(read_text(path))
    except json.JSONDecodeError:
        return {"parse_error": "invalid JSON"}
    files = data.get("Files", {}) if isinstance(data.get("Files"), dict) else {}
    args = data.get("Args", {}) if isinstance(data.get("Args"), dict) else {}
    return {
        "top": data.get("Top"),
        "rtl_top": data.get("RtlTop"),
        "function_protocol": data.get("FunctionProtocol"),
        "source_files_raw": files.get("CSource", []),
        "testbench_files_raw": files.get("TestBench", []),
        "arguments": args,
    }


def build_summary(selected: dict[str, Any], input_path: Path, candidates: list[dict[str, Any]]) -> dict[str, Any]:
    solution: Path = selected["solution"]
    artifacts: dict[str, Path | None] = selected["artifacts"]
    top_xml = artifacts["top_xml"]
    if top_xml is None:
        raise RuntimeError(f"No top-level *_csynth.xml found under {solution}")

    parsed = parse_top_xml(top_xml)
    submodule_reports = sorted(
        path for path in (solution / "syn" / "report").glob("*_csynth.rpt")
        if path != artifacts["top_rpt"]
    )
    rtl_files = sorted((solution / "syn").rglob("*.v")) + sorted((solution / "syn").rglob("*.vhd"))
    all_artifact_paths = [path for path in artifacts.values() if path is not None]

    ambiguity = False
    if len(candidates) > 1:
        first, second = candidates[0], candidates[1]
        ambiguity = first["score"] == second["score"] and abs(first["latest_epoch"] - second["latest_epoch"]) < 1.0

    return {
        "input": str(input_path),
        "selected_solution": str(solution),
        "selection": {
            "completeness_score": selected["score"],
            "ambiguous": ambiguity,
            "candidate_count": len(candidates),
            "candidates": [
                {
                    "solution": str(item["solution"]),
                    "top": item["top"],
                    "score": item["score"],
                    "latest_artifact_time": datetime.fromtimestamp(item["latest_epoch"]).astimezone().isoformat(timespec="seconds") if item["latest_epoch"] else None,
                }
                for item in candidates
            ],
        },
        "artifact_time_range": {
            "earliest": timestamp(min(all_artifact_paths, key=lambda path: path.stat().st_mtime)) if all_artifact_paths else None,
            "latest": timestamp(max(all_artifact_paths, key=lambda path: path.stat().st_mtime)) if all_artifact_paths else None,
        },
        "artifacts": {
            name: {
                "path": relative_or_absolute(path, solution),
                "exists": path is not None,
                "modified": timestamp(path),
            }
            for name, path in artifacts.items()
        },
        "top_synthesis": parsed,
        "csim": parse_csim(artifacts["csim_log"]),
        "cosim": parse_cosim(artifacts["cosim_report"]),
        "solution_log": parse_log(artifacts["solution_log"]),
        "solution_metadata": parse_solution_data(artifacts["solution_data"]),
        "ip_export": {
            "component_xml": artifacts["component_xml"] is not None,
            "export_zip": artifacts["export_zip"] is not None,
        },
        "hierarchy_inventory": {
            "submodule_csynth_reports": [relative_or_absolute(path, solution) for path in submodule_reports],
            "generated_rtl_file_count": len(rtl_files),
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Inspect an existing Vitis HLS build without modifying it.")
    parser.add_argument("directory", type=Path, help="Build directory, solution directory, or a path inside one")
    parser.add_argument("--solution", type=Path, help="Explicit solution directory when auto-selection is ambiguous")
    args = parser.parse_args()

    input_path = args.directory.expanduser().resolve()
    if not input_path.exists():
        parser.error(f"path does not exist: {input_path}")

    search_root = args.solution.expanduser().resolve() if args.solution else input_path
    if search_root.is_file():
        search_root = search_root.parent
    candidates = discover(search_root)
    if not candidates:
        print(json.dumps({"error": "No Vitis HLS solution with a top-level *_csynth.xml was found", "input": str(input_path)}, ensure_ascii=False, indent=2))
        return 2

    try:
        summary = build_summary(candidates[0], input_path, candidates)
    except (OSError, RuntimeError, ET.ParseError) as exc:
        print(json.dumps({"error": str(exc), "input": str(input_path)}, ensure_ascii=False, indent=2))
        return 1

    print(json.dumps(summary, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
