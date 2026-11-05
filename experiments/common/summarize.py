#!/usr/bin/env python3
"""把 experiments 的 raw.csv 聚合成 Markdown 中位数表。

默认适配 experiments/reproductions/001（stage/batch/... 与 hits/qsum/checksum）；
其他实验可用 --keys / --metrics / --checksum-cols 指定列。

CSV 约定：第一列 run 为第几次运行，其余列由实验自定义。
"""

import argparse
import csv
import statistics
import sys
from collections import OrderedDict


def csv_list(value: str):
    return [x.strip() for x in value.split(",") if x.strip()]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("input")
    ap.add_argument("--output", default=None)
    ap.add_argument("--keys", type=csv_list, default=["stage", "batch", "hint", "distance", "events"])
    ap.add_argument(
        "--metrics",
        type=csv_list,
        default=["p50_ns", "p90_ns", "p99_ns", "mean_ns", "min_ns", "max_ns"],
    )
    ap.add_argument("--checksum-cols", type=csv_list, default=["hits", "qsum", "checksum"])
    ap.add_argument("--title", default="实验结果（多次运行的中位数）")
    args = ap.parse_args()

    groups: "OrderedDict[tuple, list[dict]]" = OrderedDict()
    with open(args.input, newline="") as f:
        for row in csv.DictReader(f):
            key = tuple(row[k] for k in args.keys)
            groups.setdefault(key, []).append(row)

    header = args.keys + ["runs"] + args.metrics + ["校验和一致"]
    lines = [
        f"# {args.title}",
        "",
        "| " + " | ".join(header) + " |",
        "|" + "|".join(["---"] * len(header)) + "|",
    ]
    bad = 0
    for key, rows in groups.items():
        med = {}
        for m in args.metrics:
            vals = [float(r[m]) for r in rows]
            med[m] = statistics.median(vals)
        checksums = {tuple(r[c] for c in args.checksum_cols) for r in rows}
        ok = "是" if len(checksums) == 1 else "否"
        if len(checksums) != 1:
            bad += 1
        cells = list(key) + [str(len(rows))]
        for m in args.metrics:
            cells.append(f"{med[m]:.1f}" if "mean" in m else f"{med[m]:.0f}")
        cells.append(ok)
        lines.append("| " + " | ".join(cells) + " |")

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
