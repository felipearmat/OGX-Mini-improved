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


def test_frames_slightly_out_of_order_or_one_damaged_frame_are_not_a_restart(tmp_path):
    # Seen 2026-11-04: a frame 1 us before the one ahead of it (two cores) split a session;
    # a damaged frame that passes the check can carry any time.
    frames = [hello(200_000_000), report(200_160_894, 0), report(200_160_893, 1)]
    frames += [frame(probe.USB_SENT, 10_497_309)]  # alone, far back in time
    frames += [report(200_175_000, 0), hello(201_000_000)]
    (log,) = run(tmp_path, frames)
    assert "the adapter restarted" not in log
    assert "rate:" in log


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


def test_the_hybrid_splits_the_delay_into_radio_and_i2c(tmp_path):
    """The hybrid's clock frame carries how long the ESP32 held the report: a report held 20 ms
    on the ESP32 (I2C) is not the radio's, one held back on the air is."""
    tick_us = 5000.0
    frames = [hello(1_000_000), text(1_500_000, b"1.500 slot 0 ready 057e:2006 Classic: Joy-Con (L)\n")]
    on_esp32 = {1500: 20_000}
    on_air = {2500: 15_000}
    for i in range(3000):
        count = 3 * i
        arrive_esp32 = int(2_000_000 + count * tick_us + 1500 + on_air.get(i, 0))
        age = 400 + on_esp32.get(i, 0)
        payload = bytes([0, 1]) + struct.pack("<IH", count % 256, age // 10)
        frames.append(frame(probe.PAD_CLOCK, arrive_esp32 + age, payload))
        if i % 66 == 0:
            frames.append(hello(arrive_esp32 + age + 1))
    (log,) = run(tmp_path, frames)
    total = [line for line in log.splitlines() if line.startswith("# slot 0 link delay")][0]
    radio = [line for line in log.splitlines() if "the radio alone" in line][0]
    hop = [line for line in log.splitlines() if "ESP32 to RP2040" in line][0]
    assert "max 20." in total
    assert "max 15." in radio
    assert "median 0.40 ms" in hop and "max 20.40 ms" in hop


def test_subcommand_round_trip(tmp_path):
    cmd = lambda t, sub, reply: frame(probe.PAD_COMMAND, t, bytes([1, sub, reply]))
    frames = [hello(1_000_000), cmd(1_100_000, 0x48, 0), cmd(1_118_000, 0x48, 1),
              cmd(1_200_000, 0x30, 0), cmd(1_230_000, 0x30, 1), cmd(1_300_000, 0x02, 1)]
    (log,) = run(tmp_path, frames)
    assert "subcommand 0x48 answered in 18.0 ms" in log and "subcommand 0x30 answered in 30.0 ms" in log
    assert "# slot 1 subcommand round trip: min 18.0 ms, median 30.0 ms, max 30.0 ms, 2 answered" in log


def test_link_readings_and_the_log_cost(tmp_path):
    link = lambda t, what, a, b=0: frame(probe.LINK, t, bytes([0, what]) + struct.pack("<hH", a, b))
    frames = [hello(1_000_000), link(1_100_000, 1, -3), link(1_200_000, 2, 20, 79), link(1_300_000, 3, 4),
              link(1_400_000, 1, -7),
              frame(probe.COST, 1_500_000, struct.pack("<IIIIII", 150_000, 1_500, 200, 150_000_000, 450_000, 3_000)),
              hello(2_000_000)]
    (log,) = run(tmp_path, frames)
    assert "slot 0 rssi -7 channels 20/79 failed contacts 4" in log
    assert "log hooks 0.300% of a core, longest 20.0 us" in log
    assert "# slot 0 rssi: min -7, median -3, max -3 (2 readings)" in log


def test_command_bytes_and_their_acknowledgement(tmp_path):
    cmd = probe.command(probe.CMD_OPTIONS, 2, 1)
    assert cmd[:4] == b"OGX!" and len(cmd) == 8 and sum(cmd[4:]) % 256 == 0
    frames = [hello(1_000_000), frame(probe.ACK, 1_100_000, bytes([probe.CMD_OPTIONS, 2, 1]))]
    (log,) = run(tmp_path, frames)
    assert "command done: log options 2 1" in log


def test_hd_rumble_on_its_way(tmp_path):
    play = bytes([0x28, 0x88, 0x60, 0x61]) * 2
    neutral = probe.HD_NEUTRAL
    hd = lambda t, stage, blocks, depth=1: frame(probe.HD_RUMBLE, t, bytes([stage, depth]) + blocks)
    frames = [hello(1_000_000)]
    for i in range(10):  # every 15 ms from the host, taken 3 ms later
        t = 1_100_000 + i * 15_000
        frames += [hd(t, 0, play), hd(t + 3_000, 2, play, 0)]
    frames += [hd(1_300_000, 0, play), hd(1_300_100, 1, play, 15), hd(1_330_000, 0, neutral),
               hd(1_333_000, 2, neutral, 0), hd(1_400_000, 3, neutral, 0)]
    frames += [hello(2_000_000)]
    (log,) = run(tmp_path, frames)
    assert "HD Rumble on (from the host)" in log and "HD Rumble off (from the host)" in log
    assert "HD rumble in 12/s to pad 11/s" in log and "1 dropped (queue full)" in log
    assert "# HD Rumble: 12 reports from the host, 11 to the pad, 1 dropped (queue full), 0 stale, 1 stops" in log
    assert "queue wait median 3.0 ms" in log and "deepest queue 15" in log
    assert "longest host pause while playing 65.0 ms" in log


def test_a_long_session_whose_controller_clock_drifts(tmp_path):
    """The Joy-Con's clock rate moving by 100 ppm over 20 minutes: the fit over the last minute
    follows it, so a steady link shows no late reports (one line over the session gave tens)."""
    frames = [hello(1_000_000), text(1_500_000, b"1.500 slot 0 ready 057e:2006 Classic: Joy-Con (L)\n")]
    t, count, last = 2_000_000.0, 0, 0
    for i in range(80_000):  # 20 min at 15 ms
        tick = 5000.0 * (1 + 100e-6 * i / 80_000)  # the rate drifts
        t += 3 * tick
        count += 3
        at = int(t + 1500)
        frames.append(clock(at, 0, 1, count % 256))
        if i % 66 == 0:
            frames.append(hello(at + 1))
        last = at
    (log,) = run(tmp_path, frames)
    assert "later than the controller's best case" not in log
    summary = [line for line in log.splitlines() if line.startswith("# slot 0 link delay")][0]
    assert "0 reports 10 ms or more" in summary


def test_late_reports_get_one_line_a_second(tmp_path):
    frames = [hello(1_000_000), text(1_500_000, b"1.500 slot 0 ready 057e:2006 Classic: Joy-Con (L)\n")]
    last = 0
    for i in range(1500):  # 22 s; ten reports in a row 25 ms late (a burst)
        late = 25_000 if 1000 <= i < 1010 else 0
        t = last = max(2_000_000 + i * 15_000 + late, last + 200)  # arrival order
        frames.append(clock(t, 0, 1, (3 * i) % 256))
        if i % 66 == 0:
            frames.append(hello(t + 1))
    (log,) = run(tmp_path, frames)
    lines = [line for line in log.splitlines() if "later than the controller" in line]
    assert 1 <= len(lines) <= 2  # the burst falls in one or two seconds
    assert "more late)" in log


def test_a_stale_hd_report_is_not_a_queue_wait(tmp_path):
    play = bytes([0x28, 0x88, 0x60, 0x61]) * 2
    hd = lambda t, stage, depth=1: frame(probe.HD_RUMBLE, t, bytes([stage, depth]) + play)
    frames = [hello(1_000_000), hd(1_100_000, 0), hd(4_100_000, 2, 0), hd(4_100_010, 4, 0xFF),
              hd(4_200_000, 0), hd(4_201_000, 2, 0), hello(5_000_000)]
    (log,) = run(tmp_path, frames)
    assert "1 stale" in log and "queue wait median 1.0 ms" in log and "max 1.0 ms" in log


def test_the_raw_stream_replays_into_the_same_session(tmp_path):
    import gzip
    raw = tmp_path / "raw.bin.gz"
    stream = hello(1_000_000) + text(1_100_000, b"1.100 boot STEAM\n") + hello(2_000_000)
    with gzip.open(raw, "wb") as f:
        f.write(stream[:7])
        f.write(stream[7:])
    out = tmp_path / "out"
    probe.replay([str(raw)], str(out), True)
    (log,) = [p.read_text() for p in out.iterdir()]
    assert "[rp] 1.100 boot STEAM" in log


def test_a_raw_file_with_a_cut_member_still_replays(tmp_path):
    # 2026-11-04: a service restart left the hour's gzip member without its end and the next start
    # appended a new one; gzip then stopped at "invalid block type".
    import gzip
    import zlib
    first = hello(1_000_000) + text(1_100_000, b"1.100 boot STEAM\n")
    c = zlib.compressobj(9, zlib.DEFLATED, 31)
    cut = c.compress(first * 200) + c.flush(zlib.Z_SYNC_FLUSH)  # no end, as a killed writer leaves it
    later = gzip.compress(hello(500_000_000) + text(500_100_000, b"500.100 slot 0 ready\n") + hello(501_000_000))
    raw = tmp_path / "raw.bin.gz"
    raw.write_bytes(cut + later)
    out = tmp_path / "out"
    probe.replay([str(raw)], str(out), True)
    logs = "".join(p.read_text() for p in out.iterdir())
    assert "[rp] 1.100 boot STEAM" in logs and "slot 0 ready" in logs


def test_boot_events_kept_while_the_header_is_late(tmp_path):
    frames = [text(100_000, b"0.100 boot SWITCH\n")]
    frames += [frame(probe.USB_SENT, 200_000 + i * 8_000) for i in range(8000)]  # a minute of reads
    frames += [hello(70_000_000)]
    (log,) = run(tmp_path, frames)
    assert "[rp] 0.100 boot SWITCH" in log
