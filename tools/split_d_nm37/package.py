"""Create a portable board-test package from the selected formal IP."""
import argparse
import hashlib
import json
import shutil
import xml.etree.ElementTree as ET
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("ip", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    component = args.ip / "component.xml"
    if not component.is_file() or args.output.exists():
        raise ValueError("Require a formal component.xml and a new output directory")
    package = Path(__file__).resolve().parent
    shutil.copytree(package, args.output, ignore=shutil.ignore_patterns("__pycache__"))
    shutil.copytree(args.ip, args.output / "ip_repo/fsa_stream_split_d",
                    ignore=shutil.ignore_patterns("*.zip", "ip.tmp"))
    ns = {"s": "http://www.spiritconsortium.org/XMLSchema/SPIRIT/1685-2009"}
    for item in ET.parse(component).findall(".//s:file/s:name", ns):
        if not (args.output / "ip_repo/fsa_stream_split_d" / item.text).is_file():
            raise ValueError(f"Missing referenced formal IP file: {item.text}")
    files = {str(p.relative_to(args.output)).replace("\\", "/"):
             hashlib.sha256(p.read_bytes()).hexdigest()
             for p in args.output.rglob("*") if p.is_file()}
    (args.output / "delivery_manifest.json").write_text(
        json.dumps({"component_sha256": hashlib.sha256(component.read_bytes()).hexdigest(),
                    "files": files}, indent=2) + "\n", encoding="utf-8", newline="\n")
    print(f"PORTABLE_PACKAGE={args.output.resolve()}")


if __name__ == "__main__":
    main()
