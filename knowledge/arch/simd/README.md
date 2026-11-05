# SIMD：SSE / AVX / NEON 与自动向量化

> 模块: arch ｜ 状态: expanded ｜ 深度: L2 ｜ 创建: 2026-11-05 ｜ 更新: 2026-11-05
> 依据: JD 明确点名「缓存行对齐、SIMD、分支预测」（[research/01-jd-quant-cpp.md](../../../research/01-jd-quant-cpp.md)、[research/03-tech-stack-map.md](../../../research/03-tech-stack-map.md) 中 SIMD 向量化标 P1）；arch 主线第 4 站，承接 [pipeline-superscalar](../pipeline-superscalar/README.md)，为 [prefetch](../prefetch/README.md) 铺路。

## 1. 一句话定位

**单指令多数据（SIMD, Single Instruction Multiple Data）**用一条指令同时处理向量寄存器里的多个元素（数据级并行，DLP），把「每条指令的工作量」提高 4 / 8 / 16 倍，从而提高**吞吐**而不降低单条指令的**延迟**。但它只在「没有跨迭代依赖 + 连续内存 + 计算密度足够」时兑现，而且会改变浮点归约的加法顺序。面试里真正值钱的不是背指令名，而是能说清：**向量化不是开关，而是编译器在「语义是否允许」和「是否有收益」两道判断之后的结果**。

## 2. 为什么面试官爱问

- 区分度在「什么时候不生效」：只会说「AVX 一次算 8 个 float」是背结论；能解释「为什么这个循环 `-O2` 没快、为什么加了 `-ffast-math` 突然快几倍」才算懂。
- 现象题极多：同一个循环手写 intrinsic 反而不比编译器快；`-O3 -march=native` 没变化；优化后 checksum 和回测对不上。这三件事本站都能用本机实验复现。
- 与岗位直接相关：行情批量归一化、因子/协方差矩阵运算、PnL 与统计量归约、批量校验和，都是典型 SIMD 场景；但订单簿单笔更新、变长报文解析这类依赖链/分支密集的热路径往往用不上。
- 与前一站衔接：[pipeline-superscalar](../pipeline-superscalar/README.md) 讲**指令级并行（ILP）**，本站讲**数据级并行（DLP）**，两者正交且可叠加——本机实测「标量 4 累加器」比「单向量累加器」还快，而「SIMD + 多累加器」最快。

## 3. 前置知识

- [timing-counters](../timing-counters/README.md)（已展开）：批次计时与中位数，SIMD 倍速必须建立在可信测量上。
- [pipeline-superscalar](../pipeline-superscalar/README.md)（已展开）：依赖链、FP add 延迟、多累加器；本站的 `dot_accum4` 直接复用。
- [cache-hierarchy-mesi](../cache-hierarchy-mesi/README.md)（已展开）：工作集与内存带宽决定 SIMD 收益上限；本机缓存行 128B / 64B sector。
- [branch-predictor](../branch-predictor/README.md)（已展开）：循环内分支能否被 if-conversion 成掩码。
- 延伸：[cpp/modern-cpp/cpp26.md](../../cpp/modern-cpp/cpp26.md) 的 `std::simd`（本机不可用，仅作前瞻）。
- 下一站 [prefetch](../prefetch/README.md)（规划中）：SIMD 把计算压快之后，访存更容易成为瓶颈。

## 4. 核心讲解

### 4.1 一条指令，多个 lane

```text
标量（float）：              NEON fmul.4s（128-bit = 4 lane）：
s0 = a[0]*b[0]              v0 = [a0 a1 a2 a3]
s0 = a[1]*b[1]              v1 = [b0 b1 b2 b3]
s0 = a[2]*b[2]     vs       v2 = v0 * v1        ← 1 条指令，4 个结果
s0 = a[3]*b[3]              （4 次取指/译码/发射 → 1 次）
```

- **lane（通道）**：向量寄存器按元素类型切开的槽位，`float32x4_t` 是 4 个 `float`，`__m256` 是 8 个 `float`。
- SIMD 提高的是**每周期完成的元素数**，不是「一次乘法更快」：`fmul.4s` 的延迟与标量 `fmul s` 相近，甚至略高。
- 与流水线的关系：SIMD 减少前端指令数、提高每指令工作量，但**不打破数据依赖**；一条 `acc = vfmaq_f32(acc, a, b)` 的循环仍然是串行依赖链（见 §4.7）。
- 因此 `IPC × 频率` 模型里，SIMD 主要作用在「减少总指令数」，而不是提高 IPC 本身。

### 4.2 ISA 谱系与等价写法

| 平台 | ISA | 向量宽度 | float lane 数 | C++ 类型 / 头文件 | 关键约束 |
|---|---|---|---|---|---|
| x86 | SSE / SSE2 | 128 bit | 4 | `__m128` / `<emmintrin.h>` | `_mm_load_ps` 需 16B 对齐，`_mm_loadu_ps` 不要求 |
| x86 | AVX / AVX2 | 256 bit | 8 | `__m256` / `<immintrin.h>` | `_mm256_load_ps` 需 32B 对齐；AVX2 才有整数向量完整支持 |
| x86 | AVX-512 | 512 bit | 16 | `__m512` + 掩码 `k` | 掩码可直接处理尾部；部分 Intel CPU 上重负载会触发频率下降，须按目标机实测 `[单来源，未在本机验证]` |
| ARM64 | NEON / ASIMD | 128 bit | 4 | `float32x4_t` / `<arm_neon.h>` | AArch64 基线必有；`vld1q_f32` 不强制对齐 |
| ARM64 | SVE / SVE2 | 128~2048 bit 可变 | 可变 | `svfloat32_t` + predicate | 向量长度运行时决定（VLA），尾部天然用掩码 |

本机是 Apple M2 Pro / arm64，**只有 NEON，`-march=native` 不会变出 AVX**；example 里用条件编译给出 SSE2/AVX2 等价分支（x86 分支未在本机编译验证）。

三种写法做同一件事 `c[i] = a[i] * b[i]`：

```cpp
// NEON（本机验证）                          // SSE2（x86 等价，未在本机验证）
vst1q_f32(c+i, vmulq_f32(                  _mm_storeu_ps(c+i, _mm_mul_ps(
    vld1q_f32(a+i), vld1q_f32(b+i)));          _mm_loadu_ps(a+i), _mm_loadu_ps(b+i)));
// AVX2：把 _mm_ 换成 _mm256_，一次 8 个 float，循环步长改 8
```

工程上的部署问题：

- `-march=native` 生成的二进制可能在不支援该 ISA 的机器上直接 SIGILL；生产环境要么按目标机统一编译，要么用 `__attribute__((target("avx2")))` / `__builtin_cpu_supports("avx2")` 做**运行时分发**。
- 分发本身有代价（一次分支/函数指针），且向量寄存器状态会进入上下文切换保存的 FP/SIMD 状态，见 [context-switch-affinity](../../os/context-switch-affinity/README.md)。
- 本机 `-O2` 与 `-O3 -march=native` 实测几乎无差异（§5），说明**先看汇编，别迷信编译选项**。

### 4.3 自动向量化：编译器的两道判断

编译器（以 LLVM 为例）有两个向量化器：

- **Loop Vectorizer**：把循环里连续多次迭代的标量指令「加宽」成向量指令，由 cost model 选向量宽度 VF 与展开/交错因子 UF。
- **SLP Vectorizer**：把同一基本块内若干条相似、彼此独立的标量指令合并成向量指令。它不需要循环——所以 `#pragma clang loop vectorize(disable)` **关不掉 SLP**（第 3 站也踩过这个坑）。

两道判断：

1. **合法性**：有没有循环携带依赖、指针会不会重叠（运行时别名检查）、迭代次数是否可知、控制流能否 if-conversion 成掩码、有无不可向量化的调用/类型、浮点归约能不能重排。
2. **收益**：cost model 估算寄存器压力、代码体积、gather/scatter 成本；收益为负就放弃，哪怕合法。

本机最值得记住的一条证据：`sum += a[i]*b[i]` 在 `-O2` 下**被向量化了，但只向量化乘法**——汇编里是 `fmul.4s` + `ldp q`，随后跟 16 条 `fadd s0, s0, sN` 把 lane 逐个按原顺序加回标量累加器（AArch64 的 ordered reduction）。原因是**浮点加法不满足结合律**，编译器不能擅自改变舍入顺序。结果：只快 **1.35x**，远不是 4x。想真正打破碎链，只有三条路：

- 手写多累加器（ILP，见 §4.7）；
- `-ffast-math` / `-fassociative-math` 允许重结合（代价见 §4.6）；
- 手写 intrinsic 显式按 lane 归约（自己承担数值语义）。

常用控制手段：

| 手段 | 作用 | 代价 |
|---|---|---|
| `-O2` / `-O3` | Clang 在 `-O2` 已默认开启 loop + SLP 向量化 | 本机 `-O3` 无额外收益 |
| `#pragma clang loop vectorize(disable) interleave(disable)` | 关掉紧随其后循环的循环向量化与交错 | 不关 SLP；需配合内联屏障 |
| `-fno-vectorize` / `-fno-slp-vectorize` | 全局关掉两者 | 用于排查，不用于生产 |
| `__restrict` / `__builtin_assume_aligned` | 给编译器更多别名/对齐信息 | 承诺错误即未定义行为 |
| `-ffast-math` | 放开 FP 重结合与向量化归约 | NaN/Inf、符号零、舍入语义全变 |

### 4.4 自动向量化失效清单（本机汇编核对）

`example.cpp` 里放了四个 `used noinline` 探针，用 `-S` 生成汇编逐个数向量指令（§5 有实测表）：

| 形态 | 例子 | 本机结果 | 对策 |
|---|---|---|---|
| 循环携带依赖 | `c[i] = c[i-1] + a[i]` | **0 条向量指令** | 改算法（前缀和用扫描/分块），或手写按块向量化 |
| 非单位步长 | `for (i += 2) c[i] = a[i]*2` | **0 条向量指令** | 改 SoA / 紧凑布局，或手写 shuffle、gather |
| 指针可能重叠 | 无 `__restrict` 的三指针循环 | **11 条向量指令**，但带运行时别名检查 + 标量回退 | 用 `__restrict` 表达语义；本机两种写法都生成了检查，检查本身是小代价 |
| 循环内简单条件 | `c[i] = a[i] > 0.5f ? a[i]*a[i] : 0.0f` | **16 条向量指令**（被 if-conversion 成掩码选择） | 简单三元不会阻止向量化；复杂控制流 / `switch` / 提前退出才会 |
| 浮点归约 | `s += a[i]*b[i]` | 乘法向量化、**归约仍标量** | 见 §4.3 / §4.6 |
| 结构体数组 AoS | `ticks[i].price` 跨步长访问 | 通常退化为 gather 或放弃 | 改 SoA：`price[]`、`size[]` 分开存 |
| 极短循环 | N < 一个向量宽度 × 展开因子 | 版本判断 + 尾部代码占比高 | 别为几十个元素手写 intrinsic |

**什么时候不该用**：短数组、分支密集、单次调用、已经 memory-bound、结果要求与标量逐位一致且不能改累加顺序时。SIMD 是吞吐工具，不是延迟工具。

### 4.5 对齐、尾部与数据布局

- **对齐（alignment）**：x86 `_mm_load_ps` / `_mm256_load_ps` 要求 16B / 32B 对齐，违反会触发硬件异常，非对齐要用 `*_loadu_*`；NEON `vld1q_f32` 本身不要求对齐。现代 CPU 上缓存行内的非对齐访问惩罚已很小，但跨缓存行/跨页仍有代价；本机缓存行 128B（见 [cache-hierarchy-mesi](../cache-hierarchy-mesi/README.md)）。
- **尾部（tail / epilogue）**：N 不是 lane 数整数倍时，要么留一个标量尾部循环（本站做法），要么用掩码（AVX-512 的 `k`、SVE 的 predicate），要么把数组 padding 到整数倍。手写 intrinsic 必须自己处理尾部，这是最常见的越界 bug 来源。
- **SoA vs AoS**：行情快照 `{price, size, ts}` 若按数组存（AoS），批量算 `price` 之和是跨步长访问；改成三个独立数组（SoA）后就能一次载入连续 4/8 个价格。数据布局往往比换 intrinsic 收益更大，也和缓存行利用率（第 1 站）直接相关。

### 4.6 数值语义：量化场景最容易踩的坑

- 浮点加法**不满足结合律**。把 262144 个 `float` 的乘积分成 4 / 16 条链累加，和单链累加的结果并不同：本机 `dot_scalar` = 263492.406，4 链 = 263459.531（Δ −32.875），16 lane = 263449（Δ −43.4062），相对差约 **1.6e-4**。
- **FMA（融合乘加）**只舍入一次，`fmul` + `fadd` 舍入两次；向量化可能改变是否融合，也会带来微小差异。
- `-ffast-math` 还会假设没有 NaN/Inf、忽略符号零、允许改变比较与异常语义。风控阈值、订单价格校验、行情脏值检测一旦被这些假设改写，问题不在性能而在正确性。
- 工程对策（按优先级）：先定义**误差预算** → 用定点/整数或 `double` 累加 → 需要 FP 时用 Kahan / 成对归约并固定累加顺序 → 把「checksum 逐位一致 vs 容差内一致」写进验收，回测与实盘使用同一套累加顺序。
- 本站实验一刻意选纯乘法（`c[i]=a[i]*b[i]`），三个版本 `memcmp` **逐位一致**；实验二则把差异直接打印出来，用来提醒「快了 13x，结果也变了」。

### 4.7 收益为什么消失：ILP、带宽与依赖链

**① SIMD 与 ILP 正交，且 SIMD 不解决依赖链**（本机 `-O2`，N=262144）：

| 版本 | 手段 | ns/元素 | 相对标量 |
|---|---|---|---|
| `dot_scalar` | 1 条标量依赖链 | 1.1730 | 1.00x |
| `dot_auto` | 编译自动向量化，归约仍串行 | 0.8687 | 1.35x |
| `dot_simd1` | 1 个 NEON 累加器（4 lane） | 0.2906 | 4.04x |
| `dot_accum4` | **纯标量** 4 条独立链 | 0.2205 | **5.32x** |
| `dot_simd4` | 4 个 NEON 累加器（16 lane） | 0.0887 | **13.22x** |

`dot_simd1` 因为 `acc = vfmaq_f32(acc, ...)` 仍是一条循环携带依赖链，受 FMA 延迟限制；标量 4 链反而有 4 条并行链，所以更快。把两者叠加（`dot_simd4`）才有 13x。**结论：先拆依赖链，再谈向量宽度。**

**② 带宽上限**：工作集从 48 KiB 增到 192 MiB，SIMD 倍率从 5.22x 降到 2.34x（§5 实验三）。计算不再是唯一瓶颈，load/store 与缓存/内存带宽开始定价。注意本机 192 MiB 时倍率仍是 2.34x 而非 1.0x——M2 Pro 的 SLC/内存带宽较高，尚未完全饱和，这个数字**不能外推到 x86**。

**③ 编译器比你更会展开**：48 KiB 工作集下 `mul_auto` 5.22x，手写 `mul_simd` 只有 2.82x。汇编显示编译器用 `ldp q0,q1` / `ldp q2,q3` 做了 4 路展开 + 交错，而手写版每轮只处理 1 个向量。手写 intrinsic 要赢，通常还得自己展开并调度 load。

### 4.8 量化交易场景映射

- **适合**：因子批量计算、行情序列归一化、协方差/矩阵运算、回测统计量、批量 CRC/校验和、批量价格格式转换。这些是吞吐侧，数据连续、无跨迭代依赖。
- **不适合**：订单簿单笔更新（指针追逐 + 状态依赖）、变长报文解析（分支与提前退出）、关键路径上只跑几十个元素的计算、要求与历史结果逐位一致的 PnL 口径。
- **落地流程**：标量版 + checksum 正确性基线 → `-S` 看汇编确认是否向量化 → 判断依赖链/数据布局 → 才考虑 pragma 或 intrinsic → 3 次中位数 A/B → 误差预算验收 → 目标机 ISA 分发策略。顺序反了很容易「优化完结果不对且不知道哪一步引入的」。
- **低延迟视角**：SIMD 提高吞吐，但向量指令可能增大代码体积（I-cache 压力）、引入 warm-up 与频率/状态切换成本；单笔延迟路径要单独测 tail latency，而不是只看平均吞吐。

### 4.9 常见误区

- 「SIMD 让每条指令更快」：提高的是吞吐/每指令工作量，单指令延迟没有变短。
- 「`-O3 -march=native` 一定更快」：本机与 `-O2` 几乎相同，因为 `-O2` 已向量化且 ISA 固定；先看汇编。
- 「循环里有 if 就不能向量化」：简单三元会被 if-conversion 成掩码，本机探针仍生成 16 条向量指令。
- 「`__restrict` 会去掉所有别名检查」：本机两种写法都保留了运行时别名检查；它的价值是把语义说清，不是必然消掉指令。
- 「`#pragma clang loop vectorize(disable)` 就一定标量」：它不关 SLP，多个独立标量乘加仍可能被合成 `fmla.4s`；要钉死必须用内联屏障并核对汇编。
- 「手写 intrinsic 一定比编译器快」：编译器会展开、交错、调度；本站手写版在小工作集反而慢一半。
- 「向量宽度翻倍收益翻倍」：受依赖链、load 端口、带宽、寄存器压力限制，且 AVX-512 可能触发降频。
- 「浮点归约可以随便向量化」：不满足结合律，结果会变；`-ffast-math` 有正确性代价。
- 「4 lane 就该有 4x」：实际 2.6~5.2x 都出现过，取决于是否 memory-bound 与展开程度。

## 5. Example 说明

```bash
g++ -std=c++20 -O2 -Wall example.cpp -o example && ./example
# 编译选项对照
g++ -std=c++20 -O3 -march=native -Wall example.cpp -o example_native && ./example_native
# 汇编核对（macOS 上标签形如 __Z...，用函数名 grep 定位）
g++ -std=c++20 -O2 -Wall -S example.cpp -o /tmp/simd.s
grep -n 'fmul.4s\|fmla.4s\|vaddv\|fadd\ts' /tmp/simd.s
```

本机环境：Apple M2 Pro / macOS 26.5.1（Darwin 25.5.0）/ arm64 / Apple clang 17；三个版本均**无编译警告**。每个配置程序内 warmup 后跑 5 次取中位数，整体再跑 3 次取中位数；小 N 通过重复调用放大到至少 4M 元素以摊薄计时开销。

**实验一：逐元素乘法，自动向量化 vs 手写 NEON**（N=262144，3 × 1 MiB）

| 版本 | ns/元素 | 相对标量 | 结果校验 |
|---|---|---|---|
| `mul_scalar`（pragma 禁向量化） | 0.3185 | 1.00x | baseline |
| `mul_auto`（编译器自动向量化） | 0.1185 | **2.69x** | `memcmp` 逐位一致 |
| `mul_simd`（手写 NEON） | 0.1192 | **2.67x** | `memcmp` 逐位一致 |

**实验二：点积归约，SIMD 与 ILP 的叠加**（N=262144，基准 263492.406）

| 版本 | ns/元素 | 相对标量 | checksum | Δ |
|---|---|---|---|---|
| `dot_scalar` | 1.1730 | 1.00x | 263492.406 | +0 |
| `dot_auto` | 0.8687 | 1.35x | 263492.406 | +0 |
| `dot_simd1` | 0.2906 | 4.04x | 263459.531 | −32.875 |
| `dot_accum4` | 0.2205 | 5.32x | 263459.531 | −32.875 |
| `dot_simd4` | 0.0887 | **13.22x** | 263449 | −43.4062 |

**实验三：工作集扫描（三数组总共 12B/元素）**

| N | 工作集 | scalar | auto | simd | auto 倍率 | simd 倍率 |
|---|---|---|---|---|---|---|
| 4096 | 0.05 MiB | 0.2949 | 0.0565 | 0.1047 | **5.22x** | 2.82x |
| 65536 | 0.75 MiB | 0.2972 | 0.1162 | 0.1171 | 2.56x | 2.54x |
| 1048576 | 12 MiB | 0.3096 | 0.1163 | 0.1162 | 2.66x | 2.66x |
| 16777216 | 192 MiB | 0.3106 | 0.1328 | 0.1328 | **2.34x** | 2.34x |

**汇编核对**（`-O2 -S`，统计函数体内 `.4s` / `ldp q` / `vaddv` 等向量指令条数与标量 FP 指令条数）：

| 函数 | 向量指令 | 标量 FP 指令 | 结论 |
|---|---|---|---|
| `mul_scalar` | 0 | 1 | pragma 生效，纯标量 |
| `mul_auto` | 11 | 1 | `ldp q` + `fmul.4s`，4 路展开 |
| `mul_simd` | 12 | 1 | 手写 NEON，等价但只展开 1 路 |
| `dot_auto` | 9 | 21 | **乘法向量化，16 条 `fadd s` 按序归约** |
| `dot_accum4` | 0 | 12 | 加了 `KEEP_FP` 屏障后确认纯标量 4 链 |
| `dot_simd4` | 26 | 21 | `fmla.4s` × 4 累加器 + 水平归约 |
| `probe_prefix` / `probe_stride` | 0 | 1 | 依赖链 / 跨步访问使向量化失效 |
| `probe_alias` / `probe_branch` | 11 / 16 | 1 / 5 | 别名检查 + 回退 / if-conversion 成功 |

**`-O2` vs `-O3 -march=native`（3 次中位数）**

| 编译 | 实验一 auto | 实验一 simd | 实验二 dot_simd4 | 实验三 48 KiB auto |
|---|---|---|---|---|
| `-O2` | 2.69x | 2.67x | 13.22x | 5.22x |
| `-O3 -march=native` | 2.68x | 2.72x | 13.68x | 5.05x |

差异在噪声范围内：本机 arm64 的 NEON 是基线 ISA，`-O2` 已完成向量化，`-march=native` 没有额外指令可用。

**注意事项 / 方差控制**

- P/E 核与频率波动明显：单组实验一的 auto 倍率在 2.62x ~ 7.77x 之间摆动（离群组通常是标量基线被拖慢），必须 3 次取中位数，不要用单次结果下结论。
- `#pragma clang loop vectorize(disable)` 只关循环向量化；`dot_accum4` 最初被 SLP 合成 `fmla.4s`，加入 `KEEP_FP` 内联屏障后汇编才确认纯标量（与 [pipeline-superscalar §4.4](../pipeline-superscalar/README.md) 的发现一致）。
- x86 的 SSE2 / AVX2 分支仅按等价语义写出，**未在本机编译验证**；`vaddvq_f32`、`vfmaq_f32` 为 AArch64 指令。
- 192 MiB 工作集下倍率仍 2.34x，说明本机尚未完全 memory-bound；跨平台的带宽结论需各自实测。

## 6. 高频面试问答

| 问题 | 30 秒速答 |
|---|---|
| SIMD 提高的是延迟还是吞吐？ | 吞吐。单条向量指令延迟与标量相近甚至更高，收益来自一条指令处理多个 lane |
| SSE / AVX / AVX-512 / NEON 的宽度？ | 128 / 256 / 512 / 128 bit；float lane 数 4 / 8 / 16 / 4；SVE 是可变长度 |
| 自动向量化的两个必要条件？ | 语义合法（无循环携带依赖、别名/控制流可处理）+ cost model 判断有收益 |
| Loop Vectorizer 和 SLP Vectorizer 的区别？ | 前者加宽循环迭代，后者合并基本块内相似独立标量指令；pragma 只关前者 |
| 为什么浮点归约难自动向量化？ | 加法不满足结合律，重排改变舍入；本机只向量化乘法，归约仍是 16 条标量 `fadd` |
| `-ffast-math` 的代价？ | 放开重结合，但假设无 NaN/Inf、忽略符号零与异常语义，风控/校验可能出错 |
| 循环里有 if 一定不能向量化吗？ | 不一定；简单三元可 if-conversion 成掩码，复杂控制流/提前退出才阻止 |
| `__restrict` 的作用？ | 向编译器承诺指针不重叠，减少保守处理；说错就是 UB。本机仍保留运行时别名检查 |
| 对齐 load 与非对齐 load？ | x86 `_mm_load_ps` 要求 16B 对齐否则异常，用 `loadu` 放宽；NEON `vld1q_f32` 不强制 |
| 尾部元素怎么处理？ | 标量 epilogue、掩码（AVX-512/SVE 的 predicate）、或 padding 到向量宽度整数倍 |
| 为什么手写 intrinsic 不一定更快？ | 编译器会展开/交错/调度；本机小工作集下编译器 5.22x、手写 2.82x |
| SIMD 与 ILP 什么关系？ | 正交且可叠加；本机标量 4 链 5.32x > 单 NEON 累加器 4.04x，两者叠加 13.22x |
| 为什么数据量大时 SIMD 倍率下降？ | 转为访存/带宽瓶颈；本机 48 KiB → 192 MiB 倍率从 5.22x 降到 2.34x |
| 量化系统里哪些地方别用 SIMD？ | 单笔订单簿更新、变长解析、极短数组、要求逐位一致且不能改累加顺序的 PnL 口径 |
| 怎么验证真的向量化了？ | `-S` 看汇编里的 `fmul.4s` / `ldp q` / `fmla.4s`，或用 `-Rpass=loop-vectorize`；不要只看编译选项 |

## 7. 深度追问记录（落盘区）

| 日期 | 问题 | 结论摘要 | 联动更新 |
|---|---|---|---|
| 2026-11-05 | 为什么 `dot_auto` 只快 1.35x，`dot_simd4` 能快 13x？ | 编译器只把逐元素乘法向量化，FP 归约必须按原顺序（`fadd s` × 16）；`dot_simd4` 用 4 个向量累加器同时打破依赖链与 lane 并行。 | §4.3、§4.7、§5 汇编核对 |
| 2026-11-05 | 手写 NEON 为什么没比编译器快，小数组还慢一半？ | 编译器 `ldp q0,q1` 做 4 路展开 + 交错，手写版每轮 1 个向量；手写要赢需自己展开并调度 load。 | §4.7、§5 实验三 |
| 2026-11-05 | 为什么标量 `dot_accum4` 反而比 `dot_simd1` 快？ | SIMD 与 ILP 正交：单向量累加器仍是 1 条 FMA 依赖链，4 条标量链有 4 倍 ILP；先拆依赖链再谈向量宽度。 | §4.7 |
| 2026-11-05 | 为什么 `-O3 -march=native` 与 `-O2` 几乎一样？ | 本机 `-O2` 已开启向量化，NEON 是 arm64 基线 ISA，没有更宽的指令可用；x86 上是否成立需另测。 | §4.2、§5 编译选项对照 |
| 2026-11-05 | 优化后 checksum 不一致，量化计算能不能接受？ | 分链累加改变 FP 舍入顺序，本机相对差约 1.6e-4；先定误差预算，用定点/`double`/固定累加顺序，并把逐位一致或容差写进验收。 | §4.6、§5 实验二 |

## 8. 参考

- LLVM, *Auto-Vectorization in LLVM*：loop/SLP vectorizer、cost model、运行时别名检查、if-conversion、FP 归约需 `-ffast-math` 子集、AArch64 ordered reduction（[llvm.org/docs/Vectorizers.html](https://llvm.org/docs/Vectorizers.html)，核实日期 2026-11-05）
- Intel, *Intel® Intrinsics Guide*：SSE/AVX/AVX-512 intrinsic、对应指令与延迟/吞吐（[intel.com](https://www.intel.com/content/www/us/en/docs/intrinsics-guide/index.html)，核实日期 2026-11-05）
- Arm, *Intrinsics Reference*：NEON / SVE / SME intrinsic 与指令分组（[developer.arm.com](https://developer.arm.com/architectures/instruction-sets/intrinsics)，核实日期 2026-11-05）
- Bryant & O'Hallaron, *CSAPP*，第 5 章（优化程序性能：向量化与数据级并行）（核实日期 2026-11-05）
- Hennessy & Patterson, *Computer Architecture: A Quantitative Approach*，第 4 章（数据级并行 / 向量体系结构）（核实日期 2026-11-05）
- Agner Fog, *Optimizing software in C++* 与 *The Microarchitecture of Intel, AMD and VIA CPUs*（向量指令与执行端口）[agner.org](https://www.agner.org/optimize/)（核实日期 2026-11-05）
- 关联知识点：[pipeline-superscalar](../pipeline-superscalar/README.md)、[cache-hierarchy-mesi](../cache-hierarchy-mesi/README.md)、[branch-predictor](../branch-predictor/README.md)、[timing-counters](../timing-counters/README.md)、[modern-cpp/cpp26.md](../../cpp/modern-cpp/cpp26.md)
- 关联真题：`interview/system/`（待补，SIMD / 自动向量化类真题后续拆解归档）
