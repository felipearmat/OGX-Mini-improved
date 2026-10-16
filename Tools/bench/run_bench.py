#!/usr/bin/env python3
"""Flash each build and run the bench tests on it (Tools/bench/README.md).

  run_bench.py --solo PI_PICO2W.uf2
  run_bench.py --hybrid ESP32_BLUEPAD32_I2C_PICO2.uf2 --esp32 build-dir
  run_bench.py --solo A.uf2 --hybrid B.uf2 --esp32 dir      both, one after the other

The adapter must start in D-Input/Debug mode (Start + RB + Left). The Pico is flashed through its
own USB port (SYSTEM bootloader command, then picotool), the hybrid's ESP32 through the Pico's
bridge (Tools/hybrid/ogxm_hybrid.py flash-esp32). Extra arguments after "--" go to pytest.
"""
import argparse
import pathlib
import subprocess
import sys
import time

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from adapter import Adapter  # noqa: E402

HYBRID_TOOL = HERE.parent / "hybrid" / "ogxm_hybrid.py"


def flash_pico(uf2):
    a = Adapter.open()
    a.system("SYSTEM_BOOTSEL")
    a.close()
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline:
        if subprocess.run(["picotool", "info"], capture_output=True).returncode == 0:
            break
        time.sleep(0.5)
    subprocess.run(["picotool", "load", "-v", "-x", str(uf2)], check=True)
    Adapter.open(timeout=40).close()


def flash_esp32(build_dir):
    subprocess.run([sys.executable, str(HYBRID_TOOL), "flash-esp32", str(build_dir)], check=True)
    time.sleep(3)
    a = Adapter.open(timeout=60)
    try:
        a.wait_companion(timeout=40)  # the tests start with the ESP32 up
    finally:
        a.close()


def run_pytest(label, extra):
    print(f"\n=== {label} ===", flush=True)
    return subprocess.run([sys.executable, "-m", "pytest", str(HERE), "-v", *extra]).returncode


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--solo", type=pathlib.Path, help="UF2 of a Pico W / Pico 2 W (solo) build")
    parser.add_argument("--hybrid", type=pathlib.Path, help="UF2 of a hybrid build (the Pico side)")
    parser.add_argument("--esp32", type=pathlib.Path, help="ESP32 companion build directory (with --hybrid)")
    parser.add_argument("pytest_args", nargs="*")
    args = parser.parse_args()
    if not args.solo and not args.hybrid:
        return run_pytest("current firmware", args.pytest_args)
    failed = 0
    if args.solo:
        flash_pico(args.solo)
        failed += run_pytest(f"solo: {args.solo.name}", args.pytest_args) != 0
    if args.hybrid:
        flash_pico(args.hybrid)
        if args.esp32:
            flash_esp32(args.esp32)
        failed += run_pytest(f"hybrid: {args.hybrid.name}", args.pytest_args) != 0
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
