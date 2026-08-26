#!/usr/bin/env python3
import pathlib
import sys
import zipfile


def main() -> int:
    stage_dir = pathlib.Path(__file__).resolve().parent
    package_dir = stage_dir.parent
    archive = package_dir / "ip_export" / "fsa_dma_top_u280.zip"
    destination = package_dir / "ip_repo" / "fsa_dma_top"
    if not archive.is_file():
        print(f"ERROR: export archive does not exist: {archive}", file=sys.stderr)
        return 1
    if destination.exists():
        print(f"ERROR: destination already exists: {destination}", file=sys.stderr)
        print("Move it aside explicitly, then run this command again.", file=sys.stderr)
        return 1
    destination.mkdir(parents=True)
    with zipfile.ZipFile(archive) as exported_ip:
        exported_ip.extractall(destination)
    components = list(destination.rglob("component.xml"))
    if len(components) != 1:
        print(f"ERROR: expected one component.xml, found {len(components)}", file=sys.stderr)
        return 1
    print(f"IP_UNPACK_PASS={components[0]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

