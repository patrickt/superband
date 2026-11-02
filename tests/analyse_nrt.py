#!/usr/bin/env python3
"""Analyse a float32 WAV rendered by tests/render_nrt.scd.

Left channel is SuperBand audio, right channel is reported latency / 2048 as DC.

    python3 tests/analyse_nrt.py /tmp/superband_nrt.wav --expect-freq 880 --expect-latency 1024
"""

import argparse
import math
import struct
import sys


def read_float_wav(path):
    with open(path, "rb") as fh:
        data = fh.read()
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise SystemExit("not a RIFF/WAVE file")
    pos = 12
    fmt = None
    samples = None
    while pos + 8 <= len(data):
        cid = data[pos:pos + 4]
        size = struct.unpack("<I", data[pos + 4:pos + 8])[0]
        body = data[pos + 8:pos + 8 + size]
        if cid == b"fmt ":
            fmt = struct.unpack("<HHIIHH", body[:16])
        elif cid == b"data":
            samples = body
            break
        pos += 8 + size + (size & 1)
    if fmt is None or samples is None:
        raise SystemExit("missing fmt or data chunk")
    tag, channels, rate, _, _, bits = fmt
    if tag not in (3, 0xFFFE) or bits != 32:
        raise SystemExit(f"expected 32-bit float WAV, got tag={tag} bits={bits}")
    count = len(samples) // 4 // channels
    values = struct.unpack("<%df" % (count * channels), samples[: count * channels * 4])
    chans = [values[c::channels] for c in range(channels)]
    return rate, chans


def goertzel(x, freq, rate):
    w = 2.0 * math.pi * freq / rate
    c = 2.0 * math.cos(w)
    s1 = s2 = 0.0
    for v in x:
        s0 = v + c * s1 - s2
        s2, s1 = s1, s0
    re = s1 - s2 * math.cos(w)
    im = s2 * math.sin(w)
    return 2.0 * math.hypot(re, im) / max(1, len(x))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("path")
    ap.add_argument("--expect-freq", type=float, default=880.0)
    ap.add_argument("--reject-freq", type=float, default=440.0)
    ap.add_argument("--expect-latency", type=float, default=1024.0)
    ap.add_argument("--scan", action="store_true", help="report the strongest partials in the output")
    ap.add_argument("--stereo", action="store_true",
                    help="both channels carry audio; check --expect-freq and --expect-freq2 instead "
                         "of the latency channel")
    ap.add_argument("--expect-freq2", type=float, default=990.0)
    args = ap.parse_args()

    rate, chans = read_float_wav(args.path)
    left = chans[0]
    print(f"sample rate {rate}, channels {len(chans)}, frames {len(left)}")

    failures = []

    def check(ok, what):
        print(f"  [{'PASS' if ok else 'FAIL'}] {what}")
        if not ok:
            failures.append(what)

    check(all(math.isfinite(v) for v in left), "audio channel is finite")
    peak = max(abs(v) for v in left)
    check(0.05 < peak < 2.0, f"audio has sane level (peak {peak:.3f})")

    # Analyse the middle second, clear of start-up and the tail.
    start = rate
    window = left[start:start + rate // 2]
    from collections import Counter
    silent = sum(1 for v in window if abs(v) > 1e-4)
    check(silent > len(window) * 0.9, f"audio is continuous ({silent}/{len(window)} non-silent)")

    want = goertzel(window, args.expect_freq, rate)
    other = goertzel(window, args.reject_freq, rate)
    # 0.05 rather than 1.0: a phase-vocoder shift spreads energy into sidebands
    # around the partial, so the single-bin magnitude of a correct output can be
    # well below the input amplitude.
    check(want > 0.05 and want > 4.0 * other,
          f"expected {args.expect_freq:g} Hz dominates ({want:.3f} vs {other:.3f} at {args.reject_freq:g} Hz)")

    if args.scan:
        peaks = []
        f = 50.0
        while f <= 4 * args.expect_freq + 200:
            peaks.append((goertzel(window, f, rate), f))
            f += 5.0
        peaks.sort(reverse=True)
        print("  strongest partials (Hz, magnitude): " +
              ", ".join(f"{fr:.0f}:{m:.3f}" for m, fr in peaks[:5]))

    if args.stereo:
        if len(chans) < 2:
            check(False, "stereo render has two channels")
        else:
            right = chans[1][start:start + rate // 2]
            r_want = goertzel(right, args.expect_freq2, rate)
            r_other = goertzel(right, args.expect_freq, rate)
            check(all(math.isfinite(v) for v in right), "right channel is finite")
            check(r_want > 0.05 and r_want > 4.0 * r_other,
                  f"right channel carries {args.expect_freq2:g} Hz ({r_want:.3f} vs "
                  f"{r_other:.3f} at {args.expect_freq:g} Hz): per-channel independence")
    elif len(chans) > 1:
        lat = chans[1][start:start + 512]
        mean = sum(lat) / len(lat)
        spread = max(lat) - min(lat)
        reported = mean * 2048.0
        check(abs(spread) < 1e-4, f"latency output is a constant DC value (spread {spread:.2e})")
        check(abs(reported - args.expect_latency) < 1.0,
              f"reported latency {reported:.0f} frames == {args.expect_latency:.0f}")

    print(f"\n{len(failures)} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
