# experiments/ — 低延迟实验库

> 与 `knowledge/`（知识点讲解）互补：这里存放**可运行、可测量、可对比**的实验。
> 每个实验都必须给出真实环境、真实代码、真实数据和失败案例，禁止只贴结论。

## 两条实验主线

| 目录 | 定位 | 入口 |
|---|---|---|
| [reproductions/](reproductions/README.md) | 复现网络上的低延迟技术贴 / 演讲 / 博客：还原案例、在本机重测、解释偏差 | [reproductions/README.md](reproductions/README.md) |
| [agent-cases/](agent-cases/README.md) | 由 Agent 按指令构造的模拟性能优化案例：给你一个基线，让你一步一步优化 | [agent-cases/README.md](agent-cases/README.md) |

公共工具在 [common/](common/)：

- `bench.hpp`：纳秒计时、p50/p90/p99、QoS、缓存行操作（ARM64 ↔ x86）、`cpu_relax`。
- `summarize.py`：把 `raw.csv` 的多次运行聚合成 Markdown 中位数表。

## 实验清单

| 编号 | 实验 | 来源 | 平台 | 状态 | 报告 |
|---|---|---|---|---|---|
| 001 | Latency Slippage：批处理 / 预取 / 驱逐 / 双核 | [CppCon 2026 演讲 + 微信文章](reproductions/001-latency-slippage-cache-optimization/README.md) | Apple M2 Pro | 完成 | [README](reproductions/001-latency-slippage-cache-optimization/README.md) |

状态：`planned` 规划 ｜ `running` 已建环境未跑完 ｜ `complete` 基线+分步+3 次中位数+偏差分析齐全。

## 与 Agent 的约定

在对话里直接说：

| 你说 | Agent 做 |
|---|---|
| 「复现这篇：<URL>」 | 读原文 → 判断硬件差异 → 建 `reproductions/NNN-<slug>/` → 实现基线+分步版本 → 本机 3 次中位数 → 写报告 → 更新本索引 |
| 「构造一个性能优化案例，主题 X，难度 L2」 | 建 `agent-cases/NNN-<slug>/`：盲基线 + 验收指标 + 分级提示 + 参考解 → 基线必须真实编译运行 |
| 「给我第 1 级提示」 | 只揭示 `coach.md` 的下一级提示，不直接给代码 |
| 「验证我的版本」 | 编译你改过的 `src/attempt.cpp`，跑基线对比，给出是否达标的逐项报告 |
| 「对答案」 | 展示 `solution/` 与差异说明，并记录你踩过的坑 |

Agent 维护规范见 [AGENT.md](../AGENT.md) §2.G；实验报告模板见 [templates/experiment.md](../templates/experiment.md)，案例模板见 [templates/agent-case.md](../templates/agent-case.md)。

## 质量门槛（硬性）

1. **真实运行**：基线、每个优化版本、参考解都必须在本机编译运行通过；禁止“应该能跑”。
2. **真实数据**：每个配置 3 次运行取中位数，写入 `results/raw.csv` 与 `results/summary.md`；报告中注明 CPU / OS / 编译器 / 编译参数。
3. **控制变量**：业务计算函数在所有版本中保持一致；用整数校验和（hits / qsum / checksum）证明没有偷换数学。
4. **失败案例**：至少记录一个没有收益或负收益的配置，并解释微架构原因。
5. **平台限制**：macOS 不开放 PMU 性能计数器、不支持硬亲和性；不能用 Intel 的计数器结论冒充本机实测，必须写清替代指标与局限。
6. **索引自洽**：新增实验后同步更新本文件、对应子目录 README、根 README 与 CHANGELOG。

## 目录约定

```text
experiments/
├── README.md                 # 本文件
├── common/                   # 跨实验复用
│   ├── bench.hpp
│   └── summarize.py
├── reproductions/
│   ├── README.md
│   └── NNN-<slug>/
│       ├── README.md         # 实验报告（模板见 templates/experiment.md）
│       ├── Makefile
│       ├── run_all.sh        # 3 次运行 + 汇总 + verify + 环境探针
│       ├── src/              # 基线 / 各阶段 / 校验
│       ├── micro/            # 环境探针（缓存延迟、指令成本、跨核迁移）
│       └── results/          # raw.csv / summary.md / verify.txt / probe_*.txt
└── agent-cases/
    ├── README.md
    └── NNN-<slug>/
        ├── README.md         # 参与者任务书（不含答案）
        ├── src/baseline.cpp  # 可运行基线
        ├── src/attempt.cpp   # 参与者修改入口
        ├── checkpoints.md    # 分阶段验收指标
        ├── coach.md          # 分级提示（不直接贴答案）
        ├── solution/         # 参考解与解释
        └── results/
```

只提交源码、报告和数据；`build/` 已在根 `.gitignore` 中忽略。
