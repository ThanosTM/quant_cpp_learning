# brainteaser 模块 — 智力题与概率题

> 量化私募笔试/初面的标配：概率期望题、心算估算题几乎必出一两道（[research/04](../../research/04-interview-landscape.md)）；考察思维过程与临场表达，比答案本身更重要。

## 知识点清单

| 知识点 | 目录 | 状态 | 优先级 | 依据 |
|---|---|---|---|---|
| 期望值与期望的线性性 | expected-value/ | planned | P0 | 掷骰子/抛硬币类真题 |
| 条件概率与贝叶斯思维 | bayes-conditional/ | planned | P0 | 生男孩女孩类真题 |
| 排列组合与计数技巧 | combinatorics/ | planned | P0 | 生日问题、握手/配对类 |
| 心算、速算与费米估算 | mental-math/ | planned | P0 | 「估算上海有多少加油站」类 |
| 赌徒破产与随机游走 | gamblers-ruin/ | planned | P1 | 与量化业务直接相关 |
| 经典智力题全集（烧绳/25 匹马/秤球） | classic-puzzles/ | planned | P1 | 面试常客，逐题积累 |
| 博弈与策略题（取石子/海盗分金） | game-theory/ | planned | P2 | 偶尔出现 |
| 马尔可夫链与简单随机过程 | markov-chains/ | planned | P2 | 进阶储备 |

优先级定义见 [research/03-tech-stack-map.md](../../research/03-tech-stack-map.md)。

## 模块说明

- 本模块 example.cpp 的定义放宽：可以是枚举验证/蒙特卡洛模拟代码，用程序验证手算结论（这也是量化面试的加分表达方式）。
- 答题模板（落盘时写进每个知识点）：① 澄清题意与假设 → ② 从小规模/极端情形找直觉 → ③ 建立模型（期望方程/递推/对称性）→ ④ 求解并做量级 sanity check → ⑤ 主动给出蒙特卡洛验证方案。
- 典型追问链示例：抛硬币先正获胜期望 → 若不公平硬币怎么造公平 → 与赌徒破产的联系 → 期望收敛速度。
