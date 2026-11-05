#!/usr/bin/env python3
"""把 experiments 的 raw.csv 聚合成 Markdown 表（3 次运行取中位数）。

CSV 约定（所有实验共用）：
  run,stage,batch,hint,distance,events,p50_ns,p90_ns,p99_ns,mean_ns,min_ns,max_ns,hits,qsum,checksum
"""

import argparse
import csv
import statistics
import sys
from collections import OrderedDict

METRICS = ["p50_ns", "p90_ns", "p99_ns", "mean_ns", "min_ns", "max_ns"]
KEYS = ["stage", "batch", "hint", "distance", "events"]


def stage_key(value: str):
    order = {"0": 0, "1": 1, "2": 2, "3": 3, "4a": 4, "4b": 5, "4c": 6}
    return order.get(value, 99)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("input")
    ap.add_argument("--output", default=None)
    args = ap.parse_args()

    groups: "OrderedDict[tuple, list[dict]]" = OrderedDict()
    with open(args.input, newline="") as f:
        for row in csv.DictReader(f):
            key = tuple(row[k] for k in KEYS)
            groups.setdefault(key, []).append(row)

    lines = [
        "# 实验结果（3 次运行的中位数）",
        "",
        "| stage | batch | hint | distance | runs | p50 (ns) | p90 (ns) | p99 (ns) | mean (ns) | 校验和一致 |",
        "|---|---|---|---|---|---|---|---|---|---|",
    ]
    bad = 0
    for key, rows in groups.items():
        stage, batch, hint, distance, events = key
        med = {}
        for m in METRICS:
            vals = [float(r[m]) for r in rows]
            med[m] = statistics.median(vals)
        checksums = {(r["hits"], r["qsum"], r["checksum"]) for r in rows}
        ok = "是" if len(checksums) == 1 else "否"
        if len(checksums) != 1:
            bad += 1
        lines.append(
            f"| {stage} | {batch} | {hint} | {distance} | {len(rows)} | "
            f"{med['p50_ns']:.0f} | {med['p90_ns']:.0f} | {med['p99_ns']:.0f} | "
            f"{med['mean_ns']:.1f} | {ok} |"
        )

    text = "\n".join(lines) + "\n"
    if args.output:
        with open(args.output, "w") as f:
            f.write(text)
    else:
        sys.stdout.write(text)
    if bad:
        sys.stderr.write(f"warning: {bad} group(s) have inconsistent checksums\n")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
