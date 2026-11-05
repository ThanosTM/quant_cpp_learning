#!/usr/bin/env bash
# 深度扫描：32/48/64/128 层，500k 事件，观察线性搜索与反转 vector 的交叉点。
set -euo pipefail
cd "$(dirname "$0")"

make -j4 all >/dev/null

RAW=results/sweep_raw.csv
echo "run,case,ops,max_levels,batch,p50_ns,p90_ns,p99_ns,mean_ns,min_ns,max_ns,count,sum,mix" > "$RAW"

run3() {
    local label="$1"
    shift
    echo "== sweep: $label"
    for r in 1 2 3; do
        ./build/bench "$@" | grep '^CSV,' | sed "s/^CSV,/$r,/" >> "$RAW"
    done
}

for levels in 32 48 64 128; do
    events="build/events_sweep_${levels}.bin"
    ./build/bench --gen-events "$events" --ops 500000 --max-levels "$levels" >/dev/null
    for c in map-clean vector-front vector-back binary-branchy linear; do
        run3 "${levels}-${c}" --events "$events" --case "$c" --warmup 50000
    done
done

python3 ../../common/summarize.py "$RAW" --output results/sweep_summary.md \
    --keys max_levels,case,ops,batch \
    --metrics p50_ns,p90_ns,p99_ns,mean_ns \
    --checksum-cols count,sum,mix \
    --title "订单簿深度扫描（500k 事件，3 次运行中位数）"

cat results/sweep_summary.md
