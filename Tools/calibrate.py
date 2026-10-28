#!/usr/bin/env python3
"""Generate independently defined, unnormalized float WAVs and verify the CLI.

H2 = 0.6*d and H3 = 0.8*d: sqrt(H2**2 + H3**2) / fundamental = d.
Only Python's standard library is required. No hidden level matching/resampling.
"""
import argparse
import array
import json
import math
from pathlib import Path
import struct
import subprocess
import sys


def write_wav(path, samples, rate):
    data = array.array("f", samples)
    if sys.byteorder != "little":
        data.byteswap()
    payload = data.tobytes()
    fmt = struct.pack("<HHIIHH", 3, 1, rate, rate * 4, 4, 32)
    path.write_bytes(b"RIFF" + struct.pack("<I", 36 + len(payload)) + b"WAVEfmt "
                     + struct.pack("<I", 16) + fmt + b"data" + struct.pack("<I", len(payload)) + payload)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path("calibration"))
    parser.add_argument("--analyzer", type=Path)
    parser.add_argument("--rate", type=int, default=48000)
    parser.add_argument("--seconds", type=float, default=4.0)
    args = parser.parse_args()
    if not 44100 <= args.rate <= 192000 or not 2.5 <= args.seconds <= 60:
        parser.error("Use 44.1–192 kHz and 2.5–60 seconds")
    args.output.mkdir(parents=True, exist_ok=True)
    count = round(args.rate * args.seconds)
    omega = 2 * math.pi * 997.3 / args.rate
    reference = args.output / "reference-997Hz.wav"
    write_wav(reference, (0.35 * math.sin(omega * i) for i in range(count)), args.rate)
    report = {"sampleRate": args.rate, "frequencyHz": 997.3, "frames": count,
              "definition": "THD_F = 100*sqrt(H2^2+H3^2)/fundamental", "tests": []}
    for percentage in (0.0, 0.1, 1.0, 5.0, 10.0):
        ratio = percentage / 100
        post = args.output / f"post-{percentage:g}-percent.wav"
        # -6.0206 dB output gain should not change THD.
        write_wav(post, (0.175 * (math.sin(omega * i) + 0.6 * ratio * math.sin(2 * omega * i + 0.3)
                                  + 0.8 * ratio * math.cos(3 * omega * i - 0.2)) for i in range(count)), args.rate)
        entry = {"expectedPercent": percentage, "post": post.name}
        if args.analyzer:
            raw = subprocess.check_output([str(args.analyzer.resolve()), str(reference), str(post)], text=True)
            result = json.loads(raw)
            values = result["measurements"]
            assert values and all(x["status"] == "valid" and x["method"] == "harmonic_thd_f" for x in values), result
            errors = [abs(x["percent"] - percentage) for x in values]
            entry["measuredPercent"] = [x["percent"] for x in values]
            entry["maximumErrorPercentagePoints"] = max(errors)
            assert max(errors) < 0.001, entry
        report["tests"].append(entry)
    (args.output / "calibration.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
