#!/usr/bin/env bash
# 主实验：48 层买盘、2,000,000 条事件、7 个有序结构变体，每个 3 次。
set -euo pipefail
cd "$(dirname "$0")"

make -j4 all asm >/dev/null

EVENTS=build/events.bin
./build/bench --gen-events "$EVENTS" --ops 2000000 --max-levels 48 >/dev/null

RAW=results/raw.csv
mkdir -p results
echo "run,case,ops,max_levels,batch,p50_ns,p90_ns,p99_ns,mean_ns,min_ns,max_ns,count,sum,mix" > "$RAW"

run3() {
    local label="$1"
    shift
    echo "== $label"
    for r in 1 2 3; do
        ./build/bench "$@" | grep '^CSV,' | sed "s/^CSV,/$r,/" >> "$RAW"
    done
}

for c in map-clean map-polluted vector-front vector-back binary-branchy binary-branchless linear; do
    run3 "$c" --events "$EVENTS" --case "$c"
done

python3 ../../common/summarize.py "$RAW" --output results/summary.md \
    --keys case,ops,max_levels,batch \
    --metrics p50_ns,p90_ns,p99_ns,mean_ns,min_ns,max_ns \
    --checksum-cols count,sum,mix \
    --title "订单簿主实验（48 层，3 次运行中位数）"

./build/bench --events "$EVENTS" --verify > results/verify.txt

echo
echo "结果已写入 results/raw.csv、results/summary.md、results/verify.txt、results/search_asm.s"
cat results/summary.md
