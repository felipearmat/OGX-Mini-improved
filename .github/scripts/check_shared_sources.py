#!/usr/bin/env python3
"""Keep the RP2040 and ESP32 firmwares from drifting apart (Firmware/common/README.md).

Fails when:
  1. a source file name exists in both Firmware/RP2040/src and Firmware/ESP32/main without
     being listed in Firmware/common/not_shared.txt (move it to Firmware/common instead);
  2. a not_shared.txt entry no longer exists in both trees (stale entry);
  3. a file in Firmware/common is shadowed by a file at the same include path in either
     firmware (the include path would pick one or the other);
  4. a file in Firmware/common includes a platform header (pico-sdk, ESP-IDF, FreeRTOS,
     TinyUSB, BTstack): shared code must build for every board;
  5. a quoted #include in either firmware or in Firmware/common names an existing file with
     different upper / lower case: it builds on macOS / Windows, not on Linux (CI).

Usage: check_shared_sources.py [repo root]   (default: the current directory)
"""
import os
import pathlib
import re
import sys

SOURCE_SUFFIXES = {".h", ".hpp", ".c", ".cpp"}
QUOTED_INCLUDE = re.compile(r'#\s*include\s*"([^"]+)"')
PLATFORM_INCLUDE = re.compile(
    r'#\s*include\s*[<"]\s*(pico/|hardware/|esp_|driver/|freertos/|nvs|sdkconfig\.h|tusb|'
    r'btstack|uni[_.]|bsp/)'
)


def sources(root: pathlib.Path):
    return [p for p in root.rglob("*") if p.is_file() and p.suffix in SOURCE_SUFFIXES]


def exists_exact_case(base: pathlib.Path, rel: str) -> bool:
    path = base
    for part in rel.split("/"):
        if part not in (".", "..") and (not path.is_dir() or part not in os.listdir(path)):
            return False
        path = path / part
    return path.exists()


def case_mismatches(repo: pathlib.Path, tree: pathlib.Path, include_roots):
    for path in sources(tree):
        for number, text in enumerate(path.read_text(errors="replace").splitlines(), 1):
            match = QUOTED_INCLUDE.search(text)
            if not match:
                continue
            rel = match.group(1)
            bases = [path.parent, *include_roots]
            if any(exists_exact_case(b, rel) for b in bases):
                continue
            if any((b / rel).exists() for b in bases):  # found only ignoring case
                yield f"{path.relative_to(repo)}:{number}: #include \"{rel}\" does not match the file name's case"


def main() -> int:
    repo = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()
    fw = repo / "Firmware"
    rp_root, esp_root, common_root = fw / "RP2040" / "src", fw / "ESP32" / "main", fw / "common"
    errors = []

    allow = {}
    for line in (common_root / "not_shared.txt").read_text().splitlines():
        line = line.split("#", 1)[0].strip()
        if line:
            name, _, reason = line.partition(" ")
            allow[name] = reason.strip()

    rp_names = {p.name for p in sources(rp_root)}
    esp_names = {p.name for p in sources(esp_root)}
    both = rp_names & esp_names

    for name in sorted(both - allow.keys()):
        errors.append(
            f"{name}: exists in both Firmware/RP2040/src and Firmware/ESP32/main. Move the shared "
            f"code to Firmware/common, or list it in Firmware/common/not_shared.txt with the reason."
        )
    for name in sorted(allow.keys() - both):
        errors.append(f"{name}: listed in Firmware/common/not_shared.txt but not in both trees; remove the entry.")

    for path in sources(common_root):
        rel = path.relative_to(common_root)
        for tree in (rp_root, esp_root):
            if (tree / rel).exists():
                errors.append(
                    f"{(tree / rel).relative_to(repo)} shadows {path.relative_to(repo)} "
                    f"(same include path); delete the copy."
                )
        for number, text in enumerate(path.read_text(errors="replace").splitlines(), 1):
            if PLATFORM_INCLUDE.search(text):
                errors.append(f"{path.relative_to(repo)}:{number}: platform header in shared code: {text.strip()}")

    for tree in (rp_root, esp_root, common_root):
        errors.extend(case_mismatches(repo, tree, [tree, common_root]))

    if errors:
        print("Shared sources check failed:")
        for e in errors:
            print(f"  - {e}")
        return 1
    print(f"Shared sources OK: {len(list(sources(common_root)))} files in Firmware/common, "
          f"{len(allow)} platform-specific names listed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
