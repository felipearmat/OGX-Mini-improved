"""probe.py's decoder and session files, from frames built here (no board)."""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(__file__))
import probe  # noqa: E402


def frame(ftype, t_us, payload=b""):
    head = bytes([ftype, len(payload)]) + b"\0" + struct.pack("<I", t_us)
    body = head + payload
    check = (-sum(body)) & 0xFF
    return bytes([probe.SYNC]) + body[:2] + bytes([check]) + body[3:]


def hello(t_us, frames=0, dropped=0, ident=b"v1 abc PI_PICO2W"):
    return frame(probe.HELLO, t_us, struct.pack("<III", frames, dropped, 16384) + ident)


def text(t_us, line, source=b"r"):
    return frame(probe.TEXT, t_us, source + line)


def report(t_us, slot):
    return frame(probe.REPORT, t_us, bytes([slot]))


def test_frames_split_and_noise_skipped():
    # A false start (0xA5 then a length) is skipped once enough bytes came to check it.
    tail = b"".join(frame(probe.USB_SENT, 40 + i) for i in range(40))
    stream = b"\x00\xa5junk" + hello(10) + b"\xa5\x02" + report(20, 1) + frame(probe.USB_SENT, 30) + tail
    dec = probe.Decoder()
    got = []
    for i in range(0, len(stream), 3):  # arrives in pieces
        got += dec.feed(stream[i:i + 3])
    assert [(f[0], f[1]) for f in got][:3] == [(probe.HELLO, 10), (probe.REPORT, 20), (probe.USB_SENT, 30)]
    assert len(got) == 43
    assert dec.bad > 0


def run(tmp_path, frames):
    ses = probe.Session(str(tmp_path), quiet=True)
    dec = probe.Decoder()
    for f in dec.feed(b"".join(frames)):
        ses.frame(*f)
    ses.close()
    files = sorted(tmp_path.iterdir(), key=lambda p: (len(p.name), p.name))
    return [p.read_text() for p in files]


def test_session_file_lines_gaps_and_text_before_the_header(tmp_path):
    frames = [text(100_000, b"0.100 boot DINPUT_DEBUG\n"), hello(1_000_000)]
    frames += [text(1_100_000, b"1.100 slot 0 ready 057e:2006 Classic: Joy-Con (L)\n")]
    frames += [report(1_200_000 + i * 15_000, 0) for i in range(10)]
    frames += [report(1_200_000 + 9 * 15_000 + 80_000, 0)]  # an 80 ms gap
    frames += [hello(2_000_000)]
    (log,) = run(tmp_path, frames)
    assert "adapter v1 abc PI_PICO2W" in log
    assert "[rp] 0.100 boot DINPUT_DEBUG" in log  # sent before the first header: kept
    assert "slot 0: report after 80.0 ms" in log
    assert "rate: usb 0/s, slot 0 11/s" in log
    assert "# slot 0: max gap 80.0 ms, 1 gaps over 50 ms" in log


def test_no_gaps_while_a_controller_connects_again(tmp_path):
    frames = [hello(1_000_000), report(1_100_000, 1)]
    frames += [text(1_200_000, b"1.200 slot 1 gone: Joy-Con (R)\n")]
    frames += [report(9_000_000, 1), report(9_200_000, 1)]  # pairing again
    frames += [text(9_300_000, b"9.300 slot 1 ready 057e:2007 Classic: Joy-Con (R)\n")]
    frames += [report(9_310_000, 1), report(9_325_000, 1)]
    (log,) = run(tmp_path, frames)
    assert "report after" not in log
    assert "# slot 1: max gap 15.0 ms, 0 gaps over 50 ms" in log


def test_a_restart_starts_a_new_file(tmp_path):
    frames = [hello(5_000_000), text(5_100_000, b"5.100 mode change to STEAM\n")]
    frames += [text(50_000, b"0.050 boot STEAM\n"), hello(1_000_000)]
    first, second = run(tmp_path, frames)
    assert "mode change to STEAM" in first and "the adapter restarted" in first
    assert "boot STEAM" in second


def test_latency_rumble_and_cost(tmp_path):
    frames = [hello(1_000_000)]
    frames += [frame(probe.LATENCY, 1_100_000 + i, struct.pack("<I", us)) for i, us in enumerate((100, 300, 200))]
    frames += [frame(probe.RUMBLE, 1_200_000, bytes([0, 128])), frame(probe.RUMBLE, 1_300_000, bytes([0, 255]))]
    frames += [frame(probe.RUMBLE, 1_400_000, bytes([0, 0]))]
    frames += [frame(probe.COST, 1_500_000, struct.pack("<IIII", 150_000, 1_500, 200, 150_000_000))]
    frames += [hello(2_000_000)]
    (log,) = run(tmp_path, frames)
    assert "rumble on 0/128" in log and "rumble off" in log
    assert log.count("rumble on") == 1  # a change of strength is not a new line
    assert "latency avg 200 max 300 us" in log and "rumble max 255" in log
    assert "probe 1000 us/s (0.100% of a core), longest frame 10.0 us" in log
    assert "# latency (input to output driver): avg 200 us, max 300 us, 3 samples" in log


def clock(t_us, slot, kind, raw):
    return frame(probe.PAD_CLOCK, t_us, bytes([slot, kind]) + struct.pack("<I", raw))


def test_link_delay_from_the_controller_clock(tmp_path):
    """A Joy-Con timer 0.2% off its nominal 5 ms: the fitted rate takes it out; two reports held
    back by the radio (25 and 12 ms) show up as delay beyond the best case."""
    tick_us = 5010.0
    frames = [hello(1_000_000), text(1_500_000, b"1.500 slot 0 ready 057e:2006 Classic: Joy-Con (L)\n")]
    held = {1500: 25_000, 2500: 12_000}
    last = 0
    for i in range(3000):  # 45 s at 15 ms (3 counts) a report
        count = 3 * i
        sent = 2_000_000 + count * tick_us
        t = int(sent + 1500 + (i % 3) * 300 + held.get(i, 0))  # 1.5-2.1 ms on the air and in the stack
        t = last = max(t, last + 200)  # what was held back delays the next one too (arrival order)
        frames.append(clock(t, 0, 1, count % 256))
        if i % 66 == 0:
            frames.append(hello(t + 1))
    (log,) = run(tmp_path, frames)
    assert "slot 0: report 25." in log and "later than the controller's best case" in log
    summary = [line for line in log.splitlines() if line.startswith("# slot 0 link delay")][0]
    assert "Switch timer clock" in summary and "max 25." in summary
    assert "3 reports 10 ms or more" in summary  # 25 ms, the one queued behind it (+10), 12 ms
    assert "median 0." in summary  # the steady delay is the best case, not counted


def test_subcommand_round_trip(tmp_path):
    cmd = lambda t, sub, reply: frame(probe.PAD_COMMAND, t, bytes([1, sub, reply]))
    frames = [hello(1_000_000), cmd(1_100_000, 0x48, 0), cmd(1_118_000, 0x48, 1),
              cmd(1_200_000, 0x30, 0), cmd(1_230_000, 0x30, 1), cmd(1_300_000, 0x02, 1)]
    (log,) = run(tmp_path, frames)
    assert "subcommand 0x48 answered in 18.0 ms" in log and "subcommand 0x30 answered in 30.0 ms" in log
    assert "# slot 1 subcommand round trip: min 18.0 ms, avg 24.0 ms, max 30.0 ms, 2 answered" in log
