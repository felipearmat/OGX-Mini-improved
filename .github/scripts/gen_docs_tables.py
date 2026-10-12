#!/usr/bin/env python3
"""Generate the output-mode table in README.md from the firmware source, so the docs cannot
drift from the code.

Sources:
  Firmware/common/UserSettings/ModeCombos.h            button combos (MODE_COMBOS), hybrid modes (HYBRID_MODES)
  Firmware/RP2040/src/Board/ModeIndicator.cpp          LED blink count and lightbar colour per mode
  Firmware/common/USBDevice/DeviceDriver/DeviceDriverTypes.h   the list of modes

The table sits between these markers in README.md:
  <!-- BEGIN GENERATED: mode-table -->  ...  <!-- END GENERATED: mode-table -->

Usage:
  gen_docs_tables.py [repo root]           rewrite the table
  gen_docs_tables.py --check [repo root]   fail if the table is not up to date (CI)

A mode added to DeviceDriverTypes.h without an entry in MODE_INFO below fails the run, so a new
mode cannot reach the docs without a name and a description.
"""
import pathlib
import re
import sys

BEGIN = "<!-- BEGIN GENERATED: mode-table (edit .github/scripts/gen_docs_tables.py, not this table) -->"
END = "<!-- END GENERATED: mode-table -->"

# Display name, what the host sees, notes. Order = order in the table.
MODE_INFO = {
    "XINPUT": ("XInput", "Xbox 360 controller", "PC, Steam, Xbox 360 console (authenticates like an original pad)"),
    "SWITCH": ("Switch Pro", "Switch Pro Controller", "Switch / Switch 2 / Lite, PC; motion and HD rumble"),
    "DINPUT": ("DInput", "generic gamepad", "PC and other hosts that take a generic gamepad"),
    "PS4": ("PS4", "DualShock 4", "PC (complete DS4: touchpad, motion, lightbar); a PS4 console needs a licensed auth adapter"),
    "STEAM": ("STEAM", "DualSense + mouse", "Steam / SteamOS / Bazzite; touchpad as mouse"),
    "XBOXOG": ("Original Xbox", "Xbox controller (Duke)", "Original Xbox console"),
    "XBOXOG_SB": ("Original Xbox: Steel Battalion", "Steel Battalion controller", "Original Xbox console"),
    "XBOXOG_XR": ("Original Xbox: DVD remote", "Xbox DVD remote", "Original Xbox console (only in builds with the remote ROM)"),
    "PS3": ("PS3", "DualShock 3", "PS3 console (motion for Sixaxis games), PC"),
    "KBM": ("Mouse + Keyboard", "keyboard + mouse + media keys", "PCs, BIOS / UEFI, smart TVs, consoles that take a mouse and keyboard"),
    "PSCLASSIC": ("PS Classic", "PlayStation Classic controller", "PlayStation Classic console"),
    "WIIU": ("Wii U (GameCube adapter)", "Wii U GameCube adapter", "Wii U, in Super Smash Bros. only; the one mode tested with several controllers"),
    "WII": ("Wii", "Wiimote over Bluetooth", "Wii / Wii U; Pico W / Pico 2 W with a wired USB controller"),
    "PS1PS2": ("PS1 / PS2", "PlayStation controller (wired)", "PS1 / PS2 through the controller cable; Pico W / Pico 2 W"),
    "GAMECUBE": ("GameCube", "GameCube controller (wired)", "GameCube / Wii GameCube ports; Pico W / Pico 2 W"),
    "DREAMCAST": ("Dreamcast", "Dreamcast controller (wired)", "Dreamcast through the controller cable; Pico W / Pico 2 W"),
    "N64": ("N64", "N64 controller (wired)", "N64 through the controller cable; Pico W / Pico 2 W"),
    "WEBAPP": ("Web App", "web app device", "Settings and diagnostics in the browser, over USB"),
}
NOT_A_MODE = {"NONE", "UART_BRIDGE"}  # internal states, not user modes

BUTTON_NAMES = {
    "BUTTON_START": "Start", "BUTTON_BACK": "Select", "BUTTON_LB": "LB", "BUTTON_RB": "RB",
    "BUTTON_A": "A", "BUTTON_B": "B", "BUTTON_X": "X", "BUTTON_Y": "Y",
    "BUTTON_L3": "L3", "BUTTON_R3": "R3", "BUTTON_SYS": "Home", "BUTTON_MISC": "Capture",
}
DPAD_NAMES = {"DPAD_UP": "Up", "DPAD_DOWN": "Down", "DPAD_LEFT": "Left", "DPAD_RIGHT": "Right"}


def fail(message: str) -> None:
    print(f"gen_docs_tables: {message}", file=sys.stderr)
    sys.exit(1)


def parse_modes(repo: pathlib.Path):
    text = (repo / "Firmware/common/USBDevice/DeviceDriver/DeviceDriverTypes.h").read_text()
    body = re.search(r"enum class DeviceDriverType[^{]*\{(.*?)\};", text, re.S)
    if not body:
        fail("DeviceDriverType enum not found")
    names = re.findall(r"^\s*([A-Z0-9_]+)\s*(?:=\s*\d+\s*)?,?", body.group(1), re.M)
    return [n for n in names if n not in NOT_A_MODE]


def parse_combos(repo: pathlib.Path):
    text = (repo / "Firmware/common/UserSettings/ModeCombos.h").read_text()
    mapping = {}
    for args, mode in re.findall(r"\{\s*combo\(([^)]*)\),\s*DeviceDriverType::(\w+)\s*\}", text):
        parts = [p.strip() for p in args.split(",")]
        buttons = [BUTTON_NAMES.get(b.strip().replace("B::", ""), b.strip()) for b in parts[0].split("|")]
        dpad = DPAD_NAMES.get(parts[1].replace("B::", ""), parts[1]) if len(parts) > 1 else None
        mapping[mode] = " + ".join(buttons + ([dpad] if dpad else []))
    if not mapping:
        fail("MODE_COMBOS not found in ModeCombos.h")
    body = re.search(r"HYBRID_MODES\[\]\s*=\s*\{(.*?)\};", text, re.S)
    if not body:
        fail("HYBRID_MODES not found in ModeCombos.h")
    hybrid = set(re.findall(r"DeviceDriverType::(\w+)", body.group(1)))
    return mapping, hybrid


def parse_switch(text: str, function: str):
    """case labels -> result text for a switch in `function`, with fall-through and default."""
    body = re.search(function + r"\s*\([^)]*\)\s*\{(.*?)\n\}", text, re.S)
    if not body:
        fail(f"{function} not found in ModeIndicator.cpp")
    result, pending = {}, []
    for line in body.group(1).splitlines():
        for label in re.findall(r"case DeviceDriverType::(\w+):", line):
            pending.append(label)
        value = re.search(r"(?:case DeviceDriverType::\w+:|default:)\s*(.+)$", line)
        is_default = "default:" in line
        if value and (value.group(1).startswith("return") or "=" in value.group(1)):
            if is_default:
                pending.append("__default__")
            for label in pending:
                result[label] = value.group(1)
            pending = []
    return result


def parse_indicator(repo: pathlib.Path):
    text = (repo / "Firmware/RP2040/src/Board/ModeIndicator.cpp").read_text()
    blinks = {k: re.search(r"return (\d+)", v).group(1) for k, v in parse_switch(text, "blink_count").items()}
    colours = {}
    for k, v in parse_switch(text, "lightbar_color").items():
        comment = re.search(r"//\s*([^(]+)", v)
        colours[k] = comment.group(1).strip() if comment else "?"
    return blinks, colours


def render(repo: pathlib.Path) -> str:
    modes = parse_modes(repo)
    missing = [m for m in modes if m not in MODE_INFO]
    if missing:
        fail("modes without an entry in MODE_INFO (add a name and description): " + ", ".join(missing))
    stale = [m for m in MODE_INFO if m not in modes]
    if stale:
        fail("MODE_INFO entries for modes that no longer exist: " + ", ".join(stale))
    combos, hybrid = parse_combos(repo)
    blinks, colours = parse_indicator(repo)
    rows = ["| Mode | Combo (hold ~3 s) | Host sees | LED blinks | Lightbar | RP2040 + ESP32 | For |",
            "|---|---|---|---|---|---|---|"]
    for mode in MODE_INFO:
        name, sees, notes = MODE_INFO[mode]
        combo = combos.get(mode, f"fixed build: `-DOGXM_FIXED_DRIVER={mode}`")
        blink = blinks.get(mode, blinks.get("__default__", "?"))
        colour = colours.get(mode, colours.get("__default__", "?"))
        rows.append(f"| {name} | {combo} | {sees} | {blink} | {colour} | {'yes' if mode in hybrid else '—'} | {notes} |")
    return "\n".join(rows)


def main() -> int:
    args = [a for a in sys.argv[1:] if a != "--check"]
    check = "--check" in sys.argv[1:]
    repo = pathlib.Path(args[0] if args else ".").resolve()
    readme = repo / "README.md"
    text = readme.read_text()
    if BEGIN not in text or END not in text:
        fail("README.md has no generated mode-table markers")
    head, rest = text.split(BEGIN, 1)
    _, tail = rest.split(END, 1)
    new_text = f"{head}{BEGIN}\n{render(repo)}\n{END}{tail}"
    if check:
        if new_text != text:
            fail("README.md mode table is out of date: run python3 .github/scripts/gen_docs_tables.py")
        print("Docs tables OK: README mode table matches the firmware source.")
        return 0
    readme.write_text(new_text)
    print("README.md mode table regenerated.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
