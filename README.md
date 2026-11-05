# 量化私募 C++ 开发求职准备库

这是一个由 **AI Agent 维护**的个人求职准备知识库，目标岗位为**量化私募 C++ 开发工程师 / 核心系统（交易系统、行情系统等）开发工程师**。

项目采用「**岗位调研 → 知识点展开 → 真题演练 → 追问深化**」的循环迭代模式。AI Agent 每次会话先阅读本文件与 [AGENT.md](AGENT.md) 了解项目全貌与维护规则，再根据指令增补、深化并维护索引。

## 目录结构

```
05_quant_job/
├── README.md            # 本文件：项目导航与使用说明
├── AGENT.md             # AI Agent 维护规范（自举入口，Agent 必读）
├── CHANGELOG.md         # 迭代历史
├── research/            # 岗位调研：JD 分析、技术栈考点地图、面试题型全景
├── knowledge/           # 知识点库：六大模块，每个知识点 = 详细讲解 + 可运行 example + 追问记录
│   ├── cpp/             # C++ 语言与新特性
│   ├── low-latency/     # 低延迟系统编程
│   ├── network/         # 计算机网络
│   ├── os/              # 操作系统
│   ├── arch/            # 计算机体系结构
│   └── brainteaser/     # 智力题 / 概率题
├── interview/           # 笔试面试真题（按题解答）与面经归档
│   ├── cpp/  system/  algorithm/  brainteaser/
│   └── experiences/     # 面经
├── experiments/         # 实验库：外部技术贴复现 + Agent 构造的性能优化案例
│   ├── common/          # 计时、分位数、缓存行操作、结果聚合
│   ├── reproductions/   # 复现：本机重测并解释偏差
│   └── agent-cases/     # 盲基线 + 验收 + 分级提示 + 参考解
├── templates/           # 知识点 / 真题 / 面经 / 追问 / 实验 / 案例 的写作模板
└── scripts/new_topic.sh # 一键创建新知识点骨架
```

## 如何使用（与 AI 对话即可）

| 你说 | Agent 做 |
|---|---|
| 「我想开始学习 move-semantics」 | 按模板展开讲解 + 可运行 example，落盘并更新两级索引 |
| 「追问：为什么移动构造要 noexcept？」 | 解答后把 Q/A 追加到该知识点的「深度追问记录」，必要时同步正文 |
| 「记一道 XX 公司的真题：……」 | 按真题模板落盘到 interview/，给出分步解答与追问链 |
| 「把这份面经整理进来」 | 按面经模板归档到 interview/experiences/ |
| 「刷新一下岗位调研」 | 检索最新 JD / 面经，更新 research/ 并标注核实日期 |
| 「这个知识点我掌握了」 | 将状态升级为 mastered，并输出自测清单 |
| 「复现这篇低延迟文章：<URL>」 | 建 experiments/reproductions/ 实验，做硬件映射、重建案例、3 次中位数与偏差分析 |
| 「构造一个性能优化案例，难度 L2」 | 建 experiments/agent-cases/：盲基线 + 验收指标 + 分级提示 + 参考解 |
| 「给下一级提示 / 验证我的版本」 | 只揭示下一级提示 / 真实编译对比并逐项报告是否达标 |

## 知识模块与进度

状态图例：`planned` 仅规划 / `outline` 仅大纲 / `expanded` 正文+example 完成 / `mastered` 追问沉淀+自测通过

| 模块 | 已展开 | 进行中 | 规划知识点数 |
|---|---|---|---|
| cpp（C++ 与新特性） | 4（move-semantics、smart-pointers、object-model、modern-cpp） | 0 | 详见 [knowledge/cpp/README.md](knowledge/cpp/README.md) |
| low-latency（低延迟） | 1（false-sharing） | 0 | 详见 [knowledge/low-latency/README.md](knowledge/low-latency/README.md) |
| network（网络） | 0 | 0 | 详见 [knowledge/network/README.md](knowledge/network/README.md) |
| os（操作系统） | 2（context-switch-affinity、virtual-memory-tlb） | 0 | 6（规划，详见 [knowledge/os/README.md](knowledge/os/README.md)） |
| arch（体系结构） | 4（timing-counters、cache-hierarchy-mesi、branch-predictor、pipeline-superscalar） | 0 | 详见 [knowledge/arch/README.md](knowledge/arch/README.md) |
| brainteaser（智力/概率） | 0 | 0 | 详见 [knowledge/brainteaser/README.md](knowledge/brainteaser/README.md) |

知识点总索引与学习进度表见 [knowledge/README.md](knowledge/README.md)；真题索引见 [interview/README.md](interview/README.md)。

## 实验库

| 类型 | 已完成 | 说明 |
|---|---|---|
| 复现（reproductions） | 1 | [001 Latency Slippage：批处理/预取/驱逐/双核](experiments/reproductions/001-latency-slippage-cache-optimization/README.md)，本机 p50 1416 → 916~958 ns（稳定 -32%，单组最好 -35%） |
| Agent 案例（agent-cases） | 0 | 等待指令构造；规则见 [experiments/agent-cases/README.md](experiments/agent-cases/README.md) |

实验库入口与质量门槛见 [experiments/README.md](experiments/README.md)。

## 维护

维护规则、模板与质量门槛见 [AGENT.md](AGENT.md)，迭代历史见 [CHANGELOG.md](CHANGELOG.md)。
