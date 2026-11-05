#!/usr/bin/env bash
# 一键跑完本复现的全部官方配置（每个配置 3 次），并生成 results/summary.md。
set -euo pipefail
cd "$(dirname "$0")"

make -j4 all >/dev/null

RAW=results/raw.csv
mkdir -p results
echo "run,stage,batch,hint,distance,events,p50_ns,p90_ns,p99_ns,mean_ns,min_ns,max_ns,hits,qsum,checksum" > "$RAW"

run3() {
    local label="$1"
    shift
    echo "== $label"
    for r in 1 2 3; do
        ./build/bench "$@" | grep '^CSV,' | sed "s/^CSV,/$r,/" >> "$RAW"
    done
}

run3 baseline            --stage 0
run3 batch-d4            --stage 1 --batch 4
run3 batch-d8            --stage 1 --batch 8
run3 batch-d16           --stage 1 --batch 16
run3 batch-d32           --stage 1 --batch 32
run3 batch-d64           --stage 1 --batch 64
run3 prefetch-d4-t0      --stage 2 --batch 4 --hint t0
run3 prefetch-d8-t0      --stage 2 --batch 8 --hint t0
run3 prefetch-d8-stream  --stage 2 --batch 8 --hint stream
run3 evict-d4            --stage 3 --batch 4 --hint t0
run3 evict-d8            --stage 3 --batch 8 --hint t0
run3 dual-full           --stage 4a --split 48
run3 dual-compact        --stage 4b --split 48
run3 dual-publish        --stage 4c --split 48

python3 ../../common/summarize.py "$RAW" --output results/summary.md

./build/bench --stage verify > results/verify.txt
for i in 1 2 3; do ./build/probe all > "results/probe_$i.txt"; done

echo
echo "结果已写入 results/raw.csv、results/summary.md、results/verify.txt、results/probe_*.txt"
cat results/summary.md
