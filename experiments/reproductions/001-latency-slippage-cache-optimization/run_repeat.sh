#!/usr/bin/env bash
# 独立复跑关键配置（用于检验第三/四步的收益是否稳定），生成 results/repeat_*。
set -euo pipefail
cd "$(dirname "$0")"

make -j4 all >/dev/null

RAW=results/repeat_raw.csv
mkdir -p results
echo "run,stage,batch,hint,distance,events,p50_ns,p90_ns,p99_ns,mean_ns,min_ns,max_ns,hits,qsum,checksum" > "$RAW"

run3() {
    local label="$1"
    shift
    echo "== repeat: $label"
    for r in 1 2 3; do
        ./build/bench "$@" | grep '^CSV,' | sed "s/^CSV,/$r,/" >> "$RAW"
    done
}

run3 baseline       --stage 0
run3 batch-d32      --stage 1 --batch 32
run3 prefetch-d8    --stage 2 --batch 8
run3 evict-d8       --stage 3 --batch 8
run3 dual-full      --stage 4a --split 48
run3 dual-compact   --stage 4b --split 48
run3 dual-publish   --stage 4c --split 48

python3 ../../common/summarize.py "$RAW" --output results/repeat_summary.md
cat results/repeat_summary.md
