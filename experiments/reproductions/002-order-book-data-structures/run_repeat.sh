#!/usr/bin/env bash
# 独立第二组：只跑关键变体，用于检验 map/vector/linear 的相对排序是否稳定。
set -euo pipefail
cd "$(dirname "$0")"

make -j4 all >/dev/null

EVENTS=build/events.bin
if [[ ! -f "$EVENTS" ]]; then
    ./build/bench --gen-events "$EVENTS" --ops 2000000 --max-levels 48 >/dev/null
fi

RAW=results/repeat_raw.csv
echo "run,case,ops,max_levels,batch,p50_ns,p90_ns,p99_ns,mean_ns,min_ns,max_ns,count,sum,mix" > "$RAW"

run3() {
    local label="$1"
    shift
    echo "== repeat: $label"
    for r in 1 2 3; do
        ./build/bench "$@" | grep '^CSV,' | sed "s/^CSV,/$r,/" >> "$RAW"
    done
}

for c in map-clean map-polluted vector-front vector-back binary-branchy binary-branchless linear; do
    run3 "$c" --events "$EVENTS" --case "$c"
done

python3 ../../common/summarize.py "$RAW" --output results/repeat_summary.md \
    --keys case,ops,max_levels,batch \
    --metrics p50_ns,p90_ns,p99_ns,mean_ns,min_ns,max_ns \
    --checksum-cols count,sum,mix \
    --title "订单簿主实验独立复跑（48 层）"

cat results/repeat_summary.md
