## Summary

<!-- What was wrong (or what is added) and who notices it. One fix or feature per PR. -->

## Type

- [ ] Bug fix
- [ ] Feature (optional behaviour; say how it is enabled and what the default is)
- [ ] Docs / build / CI only

## Root cause

<!-- For fixes: why it happened, with file / function names. For features: the design and why. -->

## Changes

<!-- One line per file or group of files. -->

-

## Scope

- **Output modes affected:** <!-- e.g. Switch Pro, PS4, STEAM, all -->
- **Input controllers affected:** <!-- e.g. Joy-Cons, DS4, any Bluetooth pad, wired only -->
- **Boards:** <!-- e.g. Pico 2 W only, every board -->
- **Behaviour change for existing users:** <!-- none / describe (new defaults, combos, timings) -->

## Verification

**Unit tests** (see [docs/TESTING.md](../docs/TESTING.md)):

- [ ] `ctest --test-dir build-tests` passes locally
- [ ] New or updated test for this change: <!-- test name, or why it cannot be unit tested -->
- [ ] The new test fails without the fix

**Hardware:**

- Board and build: <!-- e.g. Pico 2 W, Release, MAX_GAMEPADS=1 -->
- Controllers: <!-- e.g. DS4 over Bluetooth, Joy-Con pair -->
- Host: <!-- e.g. Linux + Steam, Windows, Switch console -->
- Before / after: <!-- what you saw -->

**Not tested:** <!-- paths you could not test (other boards, wired pads, consoles) -->

## Docs

- [ ] `CHANGELOG.md` (fork section: problem, cause, fix, files, how it was verified, commits)
- [ ] `docs/UPSTREAM.md` row (offer upstream? fix / feature / fork only)
- [ ] `docs/TODO.md`, `docs/TESTING.md` or `README.md` if affected
- [ ] Docs describing changed behaviour (e.g. `Firmware/RP2040/docs/Controller_Mappings.md`)

## References

<!-- Issues, captures, other projects checked (Linux drivers, SDL, GP2040-CE, etc.). -->

## Checklist

- [ ] CI is green (unit tests and firmware builds)
- [ ] Code, comments, commits and docs in English
- [ ] No unrelated changes (formatting, submodule pointers, build files)
