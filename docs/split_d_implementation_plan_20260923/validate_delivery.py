"""Validate this plan package; creates artifacts only beside this file.

No Git, HLS, synthesis or RTL invocation. Default preserves an existing input
manifest and checks it instead of silently replacing its snapshot.
"""
import hashlib
import json
import platform
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent
PARENT = ROOT.parent
REPO = PARENT / "FSA_HLS"
OLD = PARENT / "split_d_implementation_plan_20260921"


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def inventory():
    sources = set()
    for directory in ("include/fsa/stream", "src/stream", "tests/stream", "hls/fsa_stream"):
        sources.update(p for p in (REPO / directory).rglob("*") if p.is_file())
    for name in ("AGENTS.md", "PROJECT_CONTEXT.md", "run_stream_test.ps1",
                 "skills/maintaining-project-context/SKILL.md",
                 "skills/maintaining-project-context/assets/PROJECT_CONTEXT.template.md"):
        path = REPO / name
        assert path.is_file(), path
        sources.add(path)
    # Snapshot all human-written synthesis reports, regardless of localized filename.
    sources.update(p for p in (REPO / "docs").rglob("*.md") if "fsa_stream" in p.name)
    sources.update(p for p in OLD.iterdir() if p.is_file())
    sources.add(PARENT / "8FSA.md")
    return [dict(path=p.relative_to(PARENT).as_posix(), bytes=p.stat().st_size,
                 sha256=sha(p)) for p in sorted(sources, key=lambda p: str(p))]


def main():
    manifest_path = ROOT / "source_manifest.json"
    current = inventory()
    if manifest_path.exists():
        prior = json.loads(manifest_path.read_text(encoding="utf-8"))["files"]
        assert current == prior, "Input source/old-plan snapshot changed; reassess the plan."
    else:
        manifest_path.write_text(json.dumps(dict(
            review_date="2026-09-23", repo=str(REPO), old_plan=str(OLD),
            method="SHA-256 local filesystem; no Git operations",
            caveat="Inventory coverage does not imply every file received equal-depth review.",
            files=current), ensure_ascii=False, indent=2)+"\n", encoding="utf-8")
    links_checked = 0
    docs = sorted(ROOT.glob("*.md"))
    assert len(docs) == 8, [p.name for p in docs]
    for path in docs:
        content = path.read_text(encoding="utf-8")
        assert "\ufffd" not in content, path
        assert content.count("```") % 2 == 0, path
        for target in re.findall(r"\[[^\]]+\]\(([^)]+)\)", content):
            if target.startswith(("http://", "https://", "#")):
                continue
            target = target.split("#")[0]
            # This report is generated below on the first run.
            assert target == "delivery_validation.json" or (path.parent / target).exists(), (path.name,target)
            links_checked += 1
    results = json.loads((ROOT / "reference_results.json").read_text(encoding="utf-8"))
    assert results["result"] == "PASS"
    standard = results["standard_cases"]
    assert len(standard) == 72 and len(results["mutation_checks"]) == 8
    report = dict(result="PASS", python=platform.python_version(),
        source_files_snapshotted=len(current), markdown_files=len(docs),
        local_links_checked=links_checked,
        standard_cases=len(standard), detected_mutations=len(results["mutation_checks"]),
        max_ideal_abs_error=max(x["max_abs_error"] for x in standard),
        p16_ftz_sample_error=results["p_fp16_sample"]["max_abs_error"],
        missing_repo_vendor_header=not (REPO/"third_party/vitis_hls/include/ap_int.h").exists(),
        repo_build_present=(REPO/"build").exists(),
        not_run=["repository C++ regression", "HLS Csim", "HLS synthesis", "RTL cosim",
                 "IP export", "Vivado implementation", "board test"],
        generated_files=[dict(name=p.name, bytes=p.stat().st_size,sha256=sha(p))
                         for p in sorted(ROOT.iterdir()) if p.is_file()
                         and p.name != "delivery_validation.json"])
    (ROOT/"delivery_validation.json").write_text(
        json.dumps(report,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    print(json.dumps({k:v for k,v in report.items() if k != "generated_files"},
                     ensure_ascii=False,indent=2))


if __name__ == "__main__":
    main()
