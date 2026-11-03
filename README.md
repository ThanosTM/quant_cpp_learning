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
├── templates/           # 知识点 / 真题 / 面经 / 追问 的写作模板
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

## 知识模块与进度

状态图例：`planned` 仅规划 / `outline` 仅大纲 / `expanded` 正文+example 完成 / `mastered` 追问沉淀+自测通过

| 模块 | 已展开 | 进行中 | 规划知识点数 |
|---|---|---|---|
| cpp（C++ 与新特性） | 2（move-semantics、smart-pointers） | 0 | 详见 [knowledge/cpp/README.md](knowledge/cpp/README.md) |
| low-latency（低延迟） | 1（false-sharing） | 0 | 详见 [knowledge/low-latency/README.md](knowledge/low-latency/README.md) |
| network（网络） | 0 | 0 | 详见 [knowledge/network/README.md](knowledge/network/README.md) |
| os（操作系统） | 2（context-switch-affinity、virtual-memory-tlb） | 0 | 6（规划，详见 [knowledge/os/README.md](knowledge/os/README.md)） |
| arch（体系结构） | 0 | 0 | 详见 [knowledge/arch/README.md](knowledge/arch/README.md) |
| brainteaser（智力/概率） | 0 | 0 | 详见 [knowledge/brainteaser/README.md](knowledge/brainteaser/README.md) |

知识点总索引与学习进度表见 [knowledge/README.md](knowledge/README.md)；真题索引见 [interview/README.md](interview/README.md)。

## 维护

维护规则、模板与质量门槛见 [AGENT.md](AGENT.md)，迭代历史见 [CHANGELOG.md](CHANGELOG.md)。
