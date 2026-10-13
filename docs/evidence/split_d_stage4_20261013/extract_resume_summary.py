"""Extract official archive evidence; partial synthesis never becomes a pass."""

import argparse
from collections import Counter
from datetime import datetime
import hashlib
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET


parser = argparse.ArgumentParser()
parser.add_argument("archive", type=Path)
args = parser.parse_args()
archive = args.archive
solution = archive / "build/solution1"
log = (archive / "run.log").read_text(errors="replace")
lines = log.splitlines()
commit = (archive / "COMMIT").read_text().strip()
exit_file = archive / "exit.txt"
run_exit = int(exit_file.read_text()) if exit_file.exists() else None
tags = ["EstimatedClockPeriod", "Best-caseLatency", "Worst-caseLatency",
        "Interval-min", "Interval-max", "PipelineII", "PipelineDepth",
        "Latency", "TripCount", "DSP", "FF", "LUT", "BRAM_18K", "URAM"]
reports = {}
report_hashes = {}
for path in sorted((solution / "syn/report").glob("*csynth.xml")):
    tree = ET.parse(path)
    reports[path.name] = {tag: [node.text for node in tree.findall(".//"+tag)] for tag in tags}
    report_hashes[path.name] = hashlib.sha256(path.read_bytes()).hexdigest()
modules = {}
for path in sorted((solution / "syn/verilog").glob("*.v")):
    # @(*) is a sensitivity list, not a (* attribute *) block.
    text = re.sub(r"//[^\n]*|/\*.*?\*/|\(\*(?!\)).*?\*\)", "", path.read_text(), flags=re.S)
    for match in re.finditer(r"\bmodule\s+(\w+).*?\bendmodule\b", text, re.S):
        assert match[1] not in modules, "Duplicate RTL module"
        modules[match[1]] = match[0]
counts = Counter()
if "fsa_stream_split_d" in modules:
    pattern = re.compile(r"^\s*("+"|".join(re.escape(name) for name in modules)
                         +r")\s+(?:#\s*\([\s\S]*?\)\s*)?(\w+)\s*\(", re.M)
    graph = {name: [match[1] for match in pattern.finditer(text)] for name, text in modules.items()}

    def walk(name, ancestors):
        assert name not in ancestors, "RTL instantiation cycle"
        counts[name] += 1
        for child in graph[name]:
            walk(child, ancestors+[name])

    walk("fsa_stream_split_d", [])
critical = [lines[index:index+12] for index, line in enumerate(lines) if "[HLS 200-1016]" in line]
shape = re.search(r"FSA Split-D: PE=(\d+)x\d+ HEAD_DIM=(\d+) L_MAX=(\d+)", log)
assert shape, "Missing official build configuration"
finished = "Finished Command csynth_design" in log
engine = {"FSA_SPLIT_D_QUERY_CONTEXTS": 8, "FSA_SPLIT_D_PE_HOP_CYCLES": 7,
          "FSA_SPLIT_D_QK_FEEDBACK_CYCLES": 8, "FSA_SPLIT_D_ACC_HOP_CYCLES": 11}
for parameter, value in re.findall(r"FSA Split-D engine: (\w+)=(\d+)", log):
    engine[parameter] = int(value)
summary = {
    "commit": commit, "run_exit": run_exit,
    "D": int(shape[1]), "H": int(shape[2]), "L_MAX": int(shape[3]),
    "contexts": engine["FSA_SPLIT_D_QUERY_CONTEXTS"],
    "pe_hop": engine["FSA_SPLIT_D_PE_HOP_CYCLES"],
    "qk_feedback": engine["FSA_SPLIT_D_QK_FEEDBACK_CYCLES"],
    "score_commit_distance": engine["FSA_SPLIT_D_QK_FEEDBACK_CYCLES"]-engine["FSA_SPLIT_D_PE_HOP_CYCLES"],
    "acc_hop": engine["FSA_SPLIT_D_ACC_HOP_CYCLES"],
    "configuration_scope": "Engine defaults plus official Tcl overrides; verify archived source SHA",
    "csim_pass": "[stage4-summary] 32/32" in lines and "[math-summary] 32/32" in lines
                  and "CSim done with 0 errors" in log,
    "csynth_completed": run_exit == 0 and finished and "fsa_stream_split_d_csynth.xml" in reports
                         and "fsa_stream_split_d" in modules,
    "reports": reports, "report_sha256": report_hashes,
    "rtl_counts": dict(counts), "rtl_module_declarations": len(modules),
    "warning_counts": dict(Counter(re.findall(r"WARNING: \[([^]]+)\]", log))),
    "summaries": [line for line in lines if re.match(r"\[(stage4|stage12|math|bit)-summary\]", line)],
    "ii_violations": [line for line in lines if "II Violation" in line],
    "critical_paths": critical,
    "stage4_cases": 32 if "[stage4-summary] 32/32" in lines else None,
    "stage4_words": 85824 if "[stage4-summary] 32/32" in lines else None,
    "stage4_word_scope": "Complete frozen short regression; manifest/source SHA must be verified separately",
    "archive_remote": str(archive.resolve()),
    "unexecuted": ["N1024 CSim", "CoSim", "IP export", "Vivado", "board test"],
}
for label in ["start", "end"]:
    path = archive / (label+".txt")
    if path.exists():
        summary[label] = path.read_text().strip()
if "end" in summary:
    summary["elapsed_seconds"] = (datetime.fromisoformat(summary["end"])
                                  -datetime.fromisoformat(summary["start"])).total_seconds()
design_size = solution / "syn/report/csynth_design_size.rpt"
if design_size.exists():
    summary["design_size_sha256"] = hashlib.sha256(design_size.read_bytes()).hexdigest()
flow = solution / ".autopilot/db/autopilot.flow.log"
if flow.exists():
    summary["transform_elapsed_seconds"] = re.findall(r"Command\s+transform done; ([\d.]+) sec", flow.read_text())
print(json.dumps(summary, indent=2))
