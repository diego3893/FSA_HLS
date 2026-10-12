"""Read-only extractor for archived official HLS XML, log and complete RTL tree."""

import argparse
from collections import Counter
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET


parser = argparse.ArgumentParser()
parser.add_argument("build", type=Path)
parser.add_argument("log", type=Path)
parser.add_argument("commit")
parser.add_argument("contexts", type=int)
parser.add_argument("pe_hop", type=int)
parser.add_argument("feedback", type=int)
parser.add_argument("acc_hop", type=int)
args = parser.parse_args()
tags = ["EstimatedClockPeriod", "Best-caseLatency", "Worst-caseLatency",
        "Interval-min", "Interval-max", "PipelineII", "PipelineDepth",
        "Latency", "TripCount", "DSP", "FF", "LUT", "BRAM_18K", "URAM"]
reports = {}
for path in (args.build / "syn/report").glob("*csynth.xml"):
    tree = ET.parse(path)
    reports[path.name] = {tag: [node.text for node in tree.findall(".//"+tag)] for tag in tags}
modules = {}
for path in (args.build / "syn/verilog").glob("*.v"):
    text = re.sub(r"//[^\n]*|/\*.*?\*/", "", path.read_text(), flags=re.S)
    # Attributes, indentation and several modules per file occur in generated RTL.
    for match in re.finditer(r"\bmodule\s+(\w+).*?\bendmodule\b", text, re.S):
        assert match[1] not in modules
        modules[match[1]] = match[0]
pattern = re.compile(r"^\s*("+"|".join(re.escape(name) for name in modules)+r")\s+(?:#\s*\([\s\S]*?\)\s*)?(\w+)\s*\(", re.M)
graph = {name: [match[1] for match in pattern.finditer(text)] for name, text in modules.items()}
counts = Counter()


def walk(name, ancestors):
    assert name not in ancestors, "RTL instantiation cycle"
    counts[name] += 1
    for child in graph[name]:
        walk(child, ancestors+[name])


walk("fsa_stream_split_d", [])
log = args.log.read_text()
lines = log.splitlines()
critical = []
for index, line in enumerate(lines):
    if "[HLS 200-1016]" in line:
        critical.append(lines[index:index+12])
summary = {"commit": args.commit, "contexts": args.contexts,
           "pe_hop": args.pe_hop, "qk_feedback": args.feedback,
           "score_commit_distance": args.feedback-args.pe_hop,
           "acc_hop": args.acc_hop, "reports": reports,
           "rtl_counts": dict(counts), "rtl_module_declarations": len(modules),
           "warning_counts": dict(Counter(re.findall(r"WARNING: \[([^]]+)\]", log))),
           "summaries": [line for line in lines if re.match(r"\[(stage12|math|bit)-summary\]", line)],
           "ii_violations": [line for line in lines if "II Violation" in line],
           "critical_paths": critical}
print(json.dumps(summary, indent=2))
