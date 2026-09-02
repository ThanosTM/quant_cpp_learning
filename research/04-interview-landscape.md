# 笔试面试题型全景与准备计划

> 最后核实日期：2026-09-02 ｜ 依据：[04 来源清单](#5-来源)（P2 社区面经与题库为主，结论已交叉验证）

## 1. 流程结构（国内量化私募 + 海外 HFT 综合）

海外头部（HRT、Jump、Citadel Securities、Optiver 等）2026 年聚合流程（[phantomcodeai](https://www.phantomcodeai.com/blogs/hft-quant-software-engineer-interview-guide)）：

1. HR 初筛（会问技术栈细节与低延迟经验）
2. Online Assessment（90 分钟-3 小时算法 + 少量概率题）
3. 一轮技术面（现场编码 + 效率追问）
4. **低延迟编码轮**（区分度最高：无锁队列、热路径去分配、内存布局设计、零拷贝解析）
5. C++ 深度轮（Rule of Five、CRTP、concepts vs traits、虚分发成本、ABI）
6. 市场微观结构对话（订单簿、做市、逆向选择）
7. 心算 + 概率谜题轮
8. Onsite（4-6 轮：编码深挖、交易系统设计、行为面）

国内量化私募常态（据九坤、稳博、垒昂等面经）：**笔试（算法+逻辑/概率，部分开卷长时限）→ 2-3 轮技术面（C++ 基础深挖 + 计算机基础 + 项目/系统设计）→ HR 面**；社招更重系统设计与生产经验（[九坤](https://www.nowcoder.com/enterprise/10424/interview)、[稳博](https://juejin.cn/post/7383963148347375628)、[垒昂](https://leetcode.cn/circle/discuss/l4Wg6c/view/0dqumG/)）。

## 2. 题型分布（按考察目标）

| 题型 | 常见形态 | 真实样本 |
|---|---|---|
| C++ 语言深挖 | 追问式：vtable、移动语义、const、容器底层 | 九坤：对象模型/RTTI/移动语义；富途：map vs unordered_map 底层、构造函数调虚函数 |
| 低延迟编码 | 手写无锁 SPSC 队列、热循环优化、结构体布局 | [quantt 20 题](https://www.quantt.co.uk/resources/cpp-quant-interview-questions) Q1-Q7、Q15-Q20 |
| 手写实现 | atoi、随机数生成器、智能指针 | 富途一面：随机数、atoi 模拟实现 |
| 算法题 | LeetCode 中-中难（如 200 岛屿） | 富途、稳博（逻辑+算法结合、48h 开卷项目题） |
| 系统设计 | 回测系统、行情链路、交易系统 | 稳博笔试第二题：C++ 项目与回测系统设计 |
| 计算机基础 | TCP 底层、半/全连接、进程线程、缓存 | 富途二面：TCP 三次握手及底层 |
| 智力/概率 | 期望、条件概率、组合、心算 | phantomcodeai Round 7 题型清单 |

评分侧重点（低延迟轮）：热路径分配意识、结构与访问模式的缓存友好性、避免多余分支/间接/同步、**在延迟预算内偏好简单代码**（phantomcodeai）。

## 3. 国内 vs 海外侧重差异

| 维度 | 国内量化私募 | 海外 HFT |
|---|---|---|
| 算法笔试 | 比重大，部分开卷长时限项目题 | OA 快节奏 |
| C++ 深度 | 基础概念追得很细（对象模型、关键字） | 追到内存模型/无锁/ABI |
| 领域知识 | 社招问交易/行情系统经验 | 独立微观结构轮 |
| 智力题 | 逻辑题为主 | 概率+心算高频 |

## 4. 准备计划建议（8-12 周骨架，源自 phantomcodeai 并本地化）

1. 周 1-3：C++ 深度（cpp 模块 P0）+ 每日算法 2 题。
2. 周 4-6：低延迟与体系结构（low-latency + arch P0），完成 SPSC 队列、伪共享基准等动手实验。
3. 周 7-8：网络与 OS（network + os P0），epoll 手写 echo server、TCP 抓包复现。
4. 周 9-10：真题轮刷（interview/ 按题精练）+ 概率智力题（brainteaser P0）+ 心算每日训练。
5. 周 11-12：面经复盘、系统设计演练、模拟面试。

## 5. 来源

- [phantomcodeai HFT 面试指南 2026](https://www.phantomcodeai.com/blogs/hft-quant-software-engineer-interview-guide)（P2，2026-04）
- [quantt.co.uk C++ Quant Interview Questions](https://www.quantt.co.uk/resources/cpp-quant-interview-questions)（P2，2026-06）
- [quantt.co.uk Quant Developer Interview Questions](https://www.quantt.co.uk/resources/quant-developer-interview-questions)（P2）
- [牛客 · 九坤投资面经](https://www.nowcoder.com/enterprise/10424/interview)（P2）
- [掘金 · 稳博投资笔试](https://juejin.cn/post/7383963148347375628)（P2）
- [力扣 · 垒昂资产社招面经](https://leetcode.cn/circle/discuss/l4Wg6c/view/0dqumG/)（P2）
- [CSDN · 富途 C++ 后台面经](https://blog.csdn.net/CS_SKILL/article/details/162014105)（P3）
- [CSDN · 十家头部量化私募笔试题实录](https://blog.csdn.net/5b6n7m8/article/details/161481228)（P3，仅摘要级浏览，细节未核实）
