# reproductions/ — 低延迟技术贴复现

> 目标不是“抄一遍代码”，而是回答三个问题：原文的机制在本机是否成立？成立多少？不成立的那部分被什么硬件差异吃掉了？

## 清单

| 编号 | 标题 | 原文 | 原文平台 | 本机平台 | 状态 | 结论摘要 |
|---|---|---|---|---|---|---|
| [001](001-latency-slippage-cache-optimization/README.md) | Latency Slippage：与缓存逻辑博弈 | CppCon 2026「The Latency Slippage」/ 微信文章 | Intel i9-14900KS，64B 行，36MB L3 | Apple M2 Pro，128B 行，16MB L2/cluster | 完成 | p50 1416 → 916~958 ns（稳定 -32%，单组最好 -35%），与原文 510 → 329 ns（-35.5%）在量级上一致；D=64 悬崖、NTA 反例未复现 |

## 复现规范

每个复现目录必须包含：

1. **README.md**：原文链接与核实日期、硬件映射表、案例模型、每一步的原理与实测、结果汇总、偏差/失败分析、复现命令。
2. **可运行代码**：`src/` 下基线 + 分步版本；所有版本共用同一业务计算函数。
3. **校验**：`./build/bench --stage verify` 必须输出 `PASS`，证明各版本的 `hits/qsum/checksum` 完全一致。
4. **数据**：`run_all.sh` 每个配置跑 3 次，生成 `results/raw.csv` 与 `results/summary.md`；保留 `results/verify.txt`、环境探针输出。
5. **环境探针**：至少测本机缓存层次延迟与关键指令成本；不能把原文的 Intel 计数器数字当成本机结论。
6. **偏差说明**：明确写出“哪些结论复现了、哪些没有、为什么”。

## 平台提醒（Apple Silicon）

- 缓存行 128B，但写-写一致性跟踪边界为 64B；本实验按 128B 行填充对象。
- 没有 `rdpmc/perf` 这类用户态 PMU 计数器；`kperf` 需要权限，本实验未使用。因此 LFB stall、L1 命中率等只能间接推断。
- macOS 不提供硬亲和性：`thread_policy_set(THREAD_AFFINITY_POLICY)` 返回 `KERN_NOT_SUPPORTED (46)`；只能通过 QoS 请求 P-core，线程实际落点不可控。
- `dc civac` / `dc cvac` 在 EL0 可用，且本机实测约 1.4 ns/条，不是系统调用级开销；但“指令便宜”不等于“有收益”，仍需基准验证。
- `mach_absolute_time` 24 MHz（41.67 ns/tick）；单事件 p50 有一 tick 量化误差，本实验同时报告 3 次运行中位数与均值。

## 新增复现的流程

1. 读原文，记录 URL 与核实日期；能拿到原始 slides/代码就优先使用，拿不到就明确标“按文字描述重建”。
2. 先做硬件映射：缓存行、缓存容量、可用指令、计时与计数器、线程调度。
3. 构造最小但完整的业务案例；计算函数固定，所有版本共用。
4. 先跑基线并校准工作集（必须明显超过本机最后一级缓存），再逐步加优化。
5. 每个配置 3 次运行取中位数；记录至少一个负收益配置。
6. 写报告并更新 [experiments/README.md](../README.md) 与本文件。
