# 量化私募 C++ 开发岗 JD 分析

> 最后核实日期：2026-09-02 ｜ 信息类型：公开招聘 JD + 社区分析（来源分级见 [README](README.md)）

## 1. 样本 JD 摘录

| 公司/岗位 | 核心要求（摘录） | 来源 |
|---|---|---|
| 正定私募 · C++ 开发（社招） | 重点高校本科及以上，计算机相关专业；掌握**操作系统、网络原理、计算机体系结构**基础；熟悉数据结构与算法原理与实现；熟练使用 C++、代码质量意识优秀；NOI/IOI、ACM ICPC、ASC/ISC/SC 竞赛背景加分 | [LeetCode 职位页](https://leetcode.cn/company/xrsuzding/jobs/EXPERIENCED/) |
| 上海全频思维 · C++ 开发（高频交易系统方向） | 设计/开发/维护高效可靠 C++ 代码，支撑量化交易平台核心功能；优化系统性能，确保**低延迟环境**下稳定运行；与量化研究团队协作 | [BOSS 直聘](https://m.zhipin.com/zhaopin/ac5383c1e27f475c0nB53dm1Ew~~/) |
| 某量化私募 · Quantitative Developer | 知名院校计算机/软件/电子相关专业；2-5 年经验；**分布式计算/异构计算经验**；低延迟/高性能计算经验加分；擅长编程、掌握设计模式 | [BOSS 直聘](https://www.zhipin.com/job_detail/8b0911d6b3c4f84f1HN42d66EFpR.html) |
| 天演私募 · 资深 C++ 开发（行情系统方向） | 见 [02-jd-core-system.md](02-jd-core-system.md)（核心系统岗，此处从略） | [Moka 招聘页](https://app.mokahr.com/m/social-recruitment/tianyancapital/98901#/job/cdaca8e0-37bf-4f9a-a9ae-8ac5015e524b) |

社区侧对这类岗位的共性归纳：以现代 C++（C++11/17/20）为核心，要求智能指针、模板元编程与无锁并发等高阶特性；熟练运用 Linux 系统机制（epoll、进程调度、内存管理）及 x86/ARM 体系结构知识（缓存行对齐、SIMD、分支预测）以实现微秒级低延迟；Python 作辅助语言用于策略验证与 PyBind 绑定 `[单来源，社区分析]`（[CSDN 文库](https://wenku.csdn.net/answer/7iaq1z1grz6y)）。

## 2. 共性要求提炼（本仓库知识规划的依据）

**硬性要求（几乎所有 JD 都出现）**

1. C++ 语言深度：熟练掌握现代 C++（11/14/17/20），代码风格与质量意识。
2. 数据结构与算法：原理级理解 + 手写实现能力。
3. 计算机基础三件套：操作系统、计算机网络、计算机体系结构。

**高频加分项**

1. 低延迟 / 高性能计算经验（缓存优化、无锁并发、内核旁路）。
2. Linux 系统编程（epoll、进程调度、内存管理、性能剖析）。
3. 设计模式与工程化能力（模板、CRTP、allocator、回测/交易框架设计）。
4. 分布式 / 异构计算；Python（策略验证、PyBind11 绑定）。
5. 算法竞赛背景（校招尤其明显）。

## 3. 对本仓库的直接推论

- 六大知识模块（cpp / low-latency / network / os / arch / brainteaser）与「硬性要求 + 高频加分项」一一对应。
- 校招赛道竞赛与算法权重更高，社招赛道低延迟与系统设计权重更高（见 [04-interview-landscape.md](04-interview-landscape.md)）。
- 交易/行情领域知识（订单簿、撮合、行情协议）应作为第七个隐形模块逐步补充，见 [03-tech-stack-map.md](03-tech-stack-map.md) §5。
