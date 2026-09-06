#!/usr/bin/env python3
# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

"""Reproducible whole-core workloads and interleaved binary comparisons.

Build with ./build core_perf. Keep a copy of the old executable before
rebuilding, then pass --binary before=/path/to/old --binary after=./core_perf.
The measured interval is core_perf's feed loop, excluding process startup,
file reads and terminal construction. Each sample uses a fresh terminal.
These synthetic corpora are not the unpublished corpora from issue #94.
"""

import argparse
import hashlib
import json
import os
import platform
import random
import re
import statistics
import subprocess
from pathlib import Path


PATTERNS = {
    "ascii": b"The quick brown fox jumps over the lazy dog. 0123456789 ABCDEFGHIJKLMNOPQRSTUVWXYZ\r\n",
    "sgr": b"\x1b[1;34mINFO\x1b[0m \x1b[38;2;120;180;240mrequest\x1b[0m \x1b[32m200\x1b[0m \x1b[33m42ms\x1b[0m\r\n",
    "unicode": "Hello мир 日本語 한글 café e\u0301 👩\u200d💻 👍🏽 ❤️ 🇬🇧\r\n".encode(),
    "cjk": "日本語の表示と中文字符 한국어 터미널\r\n".encode(),
}
CORPORA = [*PATTERNS, "random"]
RESULT = re.compile(r": (\d+) bytes in (\d+) us, .*save_lines=(\d+)")


def positive(value):
    number = int(value)
    if number <= 0:
        raise argparse.ArgumentTypeError("must be positive")
    return number


def binary(value):
    label, separator, path = value.partition("=")
    if not separator or not label or not path:
        raise argparse.ArgumentTypeError("expected LABEL=PATH")
    resolved = Path(path).resolve()
    if not resolved.is_file():
        raise argparse.ArgumentTypeError(f"binary does not exist: {resolved}")
    return label, resolved


def generate(work, names, size):
    corpora = {}
    for name in names:
        if name == "random":
            data = random.Random(0).randbytes(size)
        else:
            pattern = PATTERNS[name]
            data = (pattern * (size // len(pattern) + 1))[:size]
        path = work / f"{name}.bin"
        path.write_bytes(data)
        corpora[name] = {
            "path": str(path),
            "bytes": len(data),
            "sha256": hashlib.sha256(data).hexdigest(),
        }
    return corpora


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", action="append", type=binary, default=[])
    parser.add_argument("--work", type=Path, default=Path(".build-core-perf"))
    parser.add_argument("--corpus", nargs="+", choices=CORPORA, default=CORPORA)
    parser.add_argument("--size-mib", type=positive, default=16)
    parser.add_argument("--rounds", type=positive, default=5)
    parser.add_argument("--save-lines", nargs="+", type=int, default=[0, 10000])
    parser.add_argument("--cpu", type=int, help="pin all runs to one Linux CPU")
    parser.add_argument("--generate-only", action="store_true")
    args = parser.parse_args()
    if not args.generate_only and not args.binary:
        parser.error("supply --binary LABEL=PATH or --generate-only")
    if len(dict(args.binary)) != len(args.binary):
        parser.error("binary labels must be unique")
    if any(not 0 <= value <= 50000 for value in args.save_lines):
        parser.error("save-lines must be between 0 and 50000")
    if args.cpu is not None:
        if not hasattr(os, "sched_setaffinity"):
            parser.error("--cpu requires Linux CPU affinity support")
        os.sched_setaffinity(0, {args.cpu})

    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    corpora = generate(work, args.corpus, args.size_mib * 1024 * 1024)
    report = {
        "platform": platform.platform(),
        "cpu": args.cpu,
        "rounds": args.rounds,
        "corpora": corpora,
        "binaries": {
            label: {"path": str(path), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
            for label, path in args.binary
        },
        "samples": [],
    }
    output = work / "results.json"

    def save():
        output.write_text(json.dumps(report, indent=2) + "\n")

    save()
    if args.generate_only:
        print(f"Corpora and hashes: {output}")
        return

    print("Median MiB/s (fresh terminal per sample; alternating binary order)", flush=True)
    print("corpus\tsave_lines\t" + "\t".join(label for label, _ in args.binary), flush=True)
    for history in args.save_lines:
        for name, corpus in corpora.items():
            elapsed = {label: [] for label, _ in args.binary}
            for round_index in range(args.rounds):
                order = args.binary if round_index % 2 == 0 else args.binary[::-1]
                for label, path in order:
                    result = subprocess.run(
                        [str(path), corpus["path"], "1", str(history)],
                        check=True, capture_output=True, text=True, timeout=120,
                    )
                    match = RESULT.search(result.stdout)
                    if match is None:
                        raise RuntimeError(f"unexpected core_perf output: {result.stdout!r}")
                    size, micros, saved = map(int, match.groups())
                    if size != corpus["bytes"] or saved != history or micros <= 0:
                        raise RuntimeError(f"invalid core_perf measurement: {result.stdout!r}")
                    elapsed[label].append(micros)
                    report["samples"].append({
                        "binary": label, "corpus": name, "save_lines": history,
                        "round": round_index, "microseconds": micros,
                    })
                    save()
            rates = [
                corpus["bytes"] / (1024 * 1024) * 1e6 / statistics.median(elapsed[label])
                for label, _ in args.binary
            ]
            print(f"{name}\t{history}\t" + "\t".join(f"{rate:.2f}" for rate in rates), flush=True)
    print(f"Raw samples, corpus and executable hashes: {output}")


if __name__ == "__main__":
    main()
