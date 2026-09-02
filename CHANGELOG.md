# 更新日志

格式参照 Keep a Changelog，版本号主观递增；日期格式 `YYYY-MM-DD`。

## \[0.2.0] - 2026-09-02

### 新增

- 展开 `knowledge/cpp/smart-pointers`：outline → expanded（L2）。

  - 正文：所有权模型选型、`unique_ptr` 零开销与 move-only、`shared_ptr` 控制块与原子计数、`weak_ptr` 破环、线程安全三层边界与 `enable_shared_from_this`、低延迟热路径视角（联动 false-sharing）。

  - `example.cpp`：四实验（sizeof 零开销 / 分配次数 2 vs 1 / 循环引用泄漏与破环 / 多线程计数原子性），已通过 `g++ -std=c++20 -O2 -Wall` 编译运行验证。

- `interview/experiences/001`：九坤「智能指针线程安全性」考点并入 smart-pointers §4.5，待拆解清单对应项勾销。

## \[0.1.0] - 2026-09-02

### 新增

- 仓库框架初始化：README 导航、AGENT 维护规范、写作模板、`scripts/new_topic.sh` 脚手架脚本。

- `research/`：量化私募 C++ 岗 JD 分析、核心系统岗 JD 分析、技术栈考点地图、笔试面试题型全景（含来源链接，核实日期 2026-09-02）。

- `knowledge/`：六大模块（cpp / low-latency / network / os / arch / brainteaser）索引与考点规划。

  - seed 完整知识点：`cpp/move-semantics`、`low-latency/false-sharing`（正文 + 可运行 example + 追问记录示例）。

  - seed 大纲知识点：`cpp/smart-pointers`。

- `interview/`：真题索引；seed 真题 `cpp/001`（构造/析构函数中调用虚函数）；面经要点摘录 `experiences/001`。

