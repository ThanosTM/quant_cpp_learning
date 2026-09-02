# 更新日志

格式参照 Keep a Changelog，版本号主观递增；日期格式 `YYYY-MM-DD`。

## \[0.3.1] - 2026-09-02

### 新增

- `knowledge/cpp/smart-pointers` 追问落盘 4 条（README §7）：删除器零开销与 EBO 机制、手写 `MySharedPtr`（控制块双原子计数 / CAS 提升 / 集体弱份额）、`unique_ptr<T[]>` 用法与 UB 坑、`enable_shared_from_this` 三坑与异步自共享范式。

  - 配套实验文件（均已通过 `g++ -std=c++20 -O2 -Wall` 编译运行验证）：`example-ebo-deleter.cpp`、`example-my-shared-ptr.cpp`、`example-unique-array.cpp`、`example-shared-from-this.cpp`。

### 更新

- `knowledge/cpp/smart-pointers/README.md`：§4.2（EBO 与 `[[no_unique_address]]`、数组特化 UB 警示）、§4.5（自共享三坑与 `owner_before` 鉴别）正文随追问扩写；§5 收录追问衍生实验清单。

## \[0.3.0] - 2026-09-02

### 新增

- `research/05-company-recruit-scan.md`：八家量化私募（幻方、天演、佳期、世纪前沿、衍复、宽德、九坤、明汯）岗位结构、校招专项、招聘流程、薪资口径与笔面试真题扫描，49 条来源按 P0-P3 分级，含横向对比（3.1-3.4）、共性趋势（7 条）、备考启示（7 条）、待拆解真题清单（编程/概率/工程/网络四类）与信息局限说明。

### 更新

- `research/03-tech-stack-map.md`：新增编译链接（P1）、TCP 粘包（P1）、IPC 全家（P1）、回归统计（P1）考点；赌徒破产/几何概率/手写代码/订单簿等条目依据补强（八家真题实据）。

- `research/README.md`：文档清单收录 05 号调研。

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

