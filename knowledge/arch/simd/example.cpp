// SIMD：SSE / AVX / NEON 与自动向量化 — 可运行示例
// 编译: g++ -std=c++20 -O2 -Wall example.cpp -o example && ./example
// 对照: g++ -std=c++20 -O3 -march=native -Wall example.cpp -o example_native && ./example_native
// 汇编: g++ -std=c++20 -O2 -Wall -S example.cpp -o /tmp/simd.s && grep -n 'fmul.4s\|fmla.4s\|vaddv' /tmp/simd.s
//
// 本机: Apple M2 Pro / macOS / arm64（Apple clang 17），NEON 128-bit / 4 × float。
// 三个实验：
//   一、逐元素乘法：标量（禁向量化） / 自动向量化 / 手写 NEON，结果逐位一致
//   二、点积归约：标量 / 自动向量化 / 标量 4 累加器（ILP） / NEON / NEON 4 累加器（SIMD+ILP）
//   三、工作集扫描：SIMD 加速比如何随 L1 → L2 → DRAM 衰减
//
// 平台分支：ARM 用 NEON；x86 用 AVX2 / SSE2 等价写法（x86 分支未在本机编译验证）。

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#if defined(__ARM_NEON) || defined(__aarch64__)
#include <arm_neon.h>
#define SIMD_ISA "NEON (128-bit, 4 x float)"
constexpr int kLanes = 4;
#elif defined(__AVX2__)
#include <immintrin.h>
#define SIMD_ISA "AVX2 (256-bit, 8 x float)"
constexpr int kLanes = 8;
#elif defined(__SSE2__) || defined(__x86_64__)
#include <emmintrin.h>
#define SIMD_ISA "SSE2 (128-bit, 4 x float)"
constexpr int kLanes = 4;
#else
#define SIMD_ISA "portable scalar (no SIMD ISA)"
constexpr int kLanes = 1;
#endif

namespace {

std::uint64_t now_ns() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

#if defined(__clang__)
#define NO_VEC _Pragma("clang loop vectorize(disable) interleave(disable)")
#else
#define NO_VEC
#endif

// #pragma 只关循环向量化，不关 SLP（superword-level parallelism）。
// 想确保「纯标量 ILP」必须用内联屏障逐个钉住累加器，见 pipeline-superscalar §4.4。
#if defined(__aarch64__)
#define KEEP_FP(x) asm volatile("" : "+w"(x))
#else
#define KEEP_FP(x) asm volatile("" : "+x"(x))
#endif

volatile double g_sink = 0.0;

void fill(std::vector<float>& a, std::vector<float>& b) {
    for (std::size_t i = 0; i < a.size(); ++i) {
        a[i] = 1.0f + static_cast<float>(i % 17) * 0.001f;
        b[i] = 1.0f - static_cast<float>(i % 13) * 0.0005f;
    }
}

// ---------------------------------------------------------------------------
// 实验一/三：逐元素乘法 c[i] = a[i] * b[i]
// 无跨迭代依赖，是最容易自动向量化的形态；纯乘法无 FMA 差异，结果可逐位比较。
// ---------------------------------------------------------------------------

__attribute__((noinline)) void mul_scalar(const float* __restrict a,
                                          const float* __restrict b,
                                          float* __restrict c, std::size_t n) {
    NO_VEC
    for (std::size_t i = 0; i < n; ++i) c[i] = a[i] * b[i];
}

__attribute__((noinline)) void mul_auto(const float* __restrict a,
                                        const float* __restrict b,
                                        float* __restrict c, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) c[i] = a[i] * b[i];
}

__attribute__((noinline)) void mul_simd(const float* a, const float* b,
                                        float* c, std::size_t n) {
    std::size_t i = 0;
#if defined(__ARM_NEON) || defined(__aarch64__)
    for (; i + kLanes <= n; i += kLanes) {
        vst1q_f32(c + i, vmulq_f32(vld1q_f32(a + i), vld1q_f32(b + i)));
    }
#elif defined(__AVX2__)
    for (; i + kLanes <= n; i += kLanes) {
        _mm256_storeu_ps(c + i, _mm256_mul_ps(_mm256_loadu_ps(a + i),
                                              _mm256_loadu_ps(b + i)));
    }
#elif defined(__SSE2__) || defined(__x86_64__)
    for (; i + kLanes <= n; i += kLanes) {
        _mm_storeu_ps(c + i, _mm_mul_ps(_mm_loadu_ps(a + i),
                                        _mm_loadu_ps(b + i)));
    }
#endif
    for (; i < n; ++i) c[i] = a[i] * b[i];  // 尾部（不足一个向量的元素）
}

// ---------------------------------------------------------------------------
// 实验二：点积 sum(a[i]*b[i])
// 浮点加法不满足结合律，编译器不能重排归约顺序：自动向量化只覆盖乘法，
// 累加仍是标量依赖链。手写 NEON / 多累加器都要付出「结果不再逐位相同」的代价。
// ---------------------------------------------------------------------------

__attribute__((noinline)) float dot_scalar(const float* a, const float* b,
                                           std::size_t n) {
    float s = 0.0f;
    NO_VEC
    for (std::size_t i = 0; i < n; ++i) s += a[i] * b[i];
    return s;
}

__attribute__((noinline)) float dot_auto(const float* a, const float* b,
                                         std::size_t n) {
    float s = 0.0f;
    for (std::size_t i = 0; i < n; ++i) s += a[i] * b[i];
    return s;
}

// 标量 ILP：4 条独立依赖链，见 pipeline-superscalar。
__attribute__((noinline)) float dot_accum4(const float* a, const float* b,
                                           std::size_t n) {
    float s0 = 0.0f, s1 = 0.0f, s2 = 0.0f, s3 = 0.0f;
    std::size_t i = 0;
    NO_VEC
    for (; i + 4 <= n; i += 4) {
        float p0 = a[i] * b[i];
        float p1 = a[i + 1] * b[i + 1];
        float p2 = a[i + 2] * b[i + 2];
        float p3 = a[i + 3] * b[i + 3];
        KEEP_FP(p0);
        KEEP_FP(p1);
        KEEP_FP(p2);
        KEEP_FP(p3);  // 也钉住 4 个乘积，否则会被 SLP 合成为 fmul.4s
        s0 += p0;
        s1 += p1;
        s2 += p2;
        s3 += p3;
        KEEP_FP(s0);
        KEEP_FP(s1);
        KEEP_FP(s2);
        KEEP_FP(s3);
    }
    float s = (s0 + s1) + (s2 + s3);
    NO_VEC
    for (; i < n; ++i) s += a[i] * b[i];  // 尾部也禁用向量化，便于汇编核对
    return s;
}

// SIMD + ILP：4 个向量累加器 = 16 条独立 lane 链（NEON）。
__attribute__((noinline)) float dot_simd4(const float* a, const float* b,
                                          std::size_t n) {
    std::size_t i = 0;
    float s = 0.0f;
#if defined(__ARM_NEON) || defined(__aarch64__)
    float32x4_t v0 = vdupq_n_f32(0.0f), v1 = vdupq_n_f32(0.0f);
    float32x4_t v2 = vdupq_n_f32(0.0f), v3 = vdupq_n_f32(0.0f);
    for (; i + 16 <= n; i += 16) {
        v0 = vfmaq_f32(v0, vld1q_f32(a + i), vld1q_f32(b + i));
        v1 = vfmaq_f32(v1, vld1q_f32(a + i + 4), vld1q_f32(b + i + 4));
        v2 = vfmaq_f32(v2, vld1q_f32(a + i + 8), vld1q_f32(b + i + 8));
        v3 = vfmaq_f32(v3, vld1q_f32(a + i + 12), vld1q_f32(b + i + 12));
    }
    float32x4_t acc = vaddq_f32(vaddq_f32(v0, v1), vaddq_f32(v2, v3));
    s = vaddvq_f32(acc);
#elif defined(__AVX2__)
    __m256 v0 = _mm256_setzero_ps(), v1 = _mm256_setzero_ps();
    __m256 v2 = _mm256_setzero_ps(), v3 = _mm256_setzero_ps();
    for (; i + 32 <= n; i += 32) {
        v0 = _mm256_add_ps(v0, _mm256_mul_ps(_mm256_loadu_ps(a + i), _mm256_loadu_ps(b + i)));
        v1 = _mm256_add_ps(v1, _mm256_mul_ps(_mm256_loadu_ps(a + i + 8), _mm256_loadu_ps(b + i + 8)));
        v2 = _mm256_add_ps(v2, _mm256_mul_ps(_mm256_loadu_ps(a + i + 16), _mm256_loadu_ps(b + i + 16)));
        v3 = _mm256_add_ps(v3, _mm256_mul_ps(_mm256_loadu_ps(a + i + 24), _mm256_loadu_ps(b + i + 24)));
    }
    __m256 acc = _mm256_add_ps(_mm256_add_ps(v0, v1), _mm256_add_ps(v2, v3));
    alignas(32) float lanes[8];
    _mm256_store_ps(lanes, acc);
    for (float v : lanes) s += v;
#elif defined(__SSE2__) || defined(__x86_64__)
    __m128 acc = _mm_setzero_ps();
    for (; i + 4 <= n; i += 4) {
        acc = _mm_add_ps(acc, _mm_mul_ps(_mm_loadu_ps(a + i), _mm_loadu_ps(b + i)));
    }
    alignas(16) float lanes[4];
    _mm_store_ps(lanes, acc);
    for (float v : lanes) s += v;
#endif
    for (; i < n; ++i) s += a[i] * b[i];
    return s;
}

// 单向量累加器：只有 SIMD，没有跨累加器 ILP，用于观察依赖链仍然存在。
__attribute__((noinline)) float dot_simd1(const float* a, const float* b,
                                          std::size_t n) {
    std::size_t i = 0;
    float s = 0.0f;
#if defined(__ARM_NEON) || defined(__aarch64__)
    float32x4_t acc = vdupq_n_f32(0.0f);
    for (; i + 4 <= n; i += 4) {
        acc = vfmaq_f32(acc, vld1q_f32(a + i), vld1q_f32(b + i));
    }
    s = vaddvq_f32(acc);
#elif defined(__AVX2__)
    __m256 acc = _mm256_setzero_ps();
    for (; i + 8 <= n; i += 8) {
        acc = _mm256_add_ps(acc, _mm256_mul_ps(_mm256_loadu_ps(a + i), _mm256_loadu_ps(b + i)));
    }
    alignas(32) float lanes[8];
    _mm256_store_ps(lanes, acc);
    for (float v : lanes) s += v;
#elif defined(__SSE2__) || defined(__x86_64__)
    __m128 acc = _mm_setzero_ps();
    for (; i + 4 <= n; i += 4) {
        acc = _mm_add_ps(acc, _mm_mul_ps(_mm_loadu_ps(a + i), _mm_loadu_ps(b + i)));
    }
    alignas(16) float lanes[4];
    _mm_store_ps(lanes, acc);
    for (float v : lanes) s += v;
#endif
    for (; i < n; ++i) s += a[i] * b[i];
    return s;
}

// ---------------------------------------------------------------------------
// 汇编核对探针：四个「自动向量化会打折/失效」的典型形态。
// 这些函数不参与计时，用 __attribute__((used)) 保证 -S 时仍生成代码；
// 用 `g++ -S ... && grep` 逐个确认是否出现向量指令（见 README §5）。
// ---------------------------------------------------------------------------

// (1) 指针可能重叠：编译器必须生成运行时别名检查 + 标量回退路径。
__attribute__((used, noinline)) void probe_alias(const float* a, const float* b,
                                                 float* c, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) c[i] = a[i] * b[i];
}

// (2) 循环携带依赖：c[i] 依赖 c[i-1]，不能并行处理相邻元素。
__attribute__((used, noinline)) void probe_prefix(const float* a, float* c,
                                                  std::size_t n) {
    for (std::size_t i = 1; i < n; ++i) c[i] = c[i - 1] + a[i];
}

// (3) 非单位步长：需要解交织/收集，收益不够时编译器放弃。
__attribute__((used, noinline)) void probe_stride(const float* a, float* c,
                                                  std::size_t n) {
    for (std::size_t i = 0; i < n; i += 2) c[i] = a[i] * 2.0f;
}

// (4) 循环内条件：能否被 if-conversion 成掩码向量指令取决于形态。
__attribute__((used, noinline)) void probe_branch(const float* a, float* c,
                                                  std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        c[i] = a[i] > 0.5f ? a[i] * a[i] : 0.0f;
    }
}

// 每个配置 5 次取中位数；小 N 通过 iters 放大到至少 4M 元素，摊薄计时开销。
template <typename Fn>
double bench_ns_per_elt(Fn fn, std::size_t n, int reps = 5) {
    constexpr std::size_t kTarget = 1u << 22;
    const std::size_t iters = std::max<std::size_t>(1, kTarget / n);
    fn();  // warmup（同时把页预热）
    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(reps));
    for (int r = 0; r < reps; ++r) {
        double checksum = 0.0;
        const std::uint64_t t0 = now_ns();
        for (std::size_t k = 0; k < iters; ++k) checksum += fn();
        const std::uint64_t t1 = now_ns();
        g_sink += checksum;
        samples.push_back(static_cast<double>(t1 - t0) /
                          static_cast<double>(iters * n));
    }
    std::sort(samples.begin(), samples.end());
    return samples[static_cast<std::size_t>(reps) / 2];
}

}  // namespace

int main() {
    std::printf("== SIMD / 自动向量化（%s）==\n", SIMD_ISA);
    constexpr std::size_t kN = 1u << 18;  // 262144 floats = 1 MiB / 数组
    std::vector<float> a(kN), b(kN), c_scalar(kN), c_auto(kN), c_simd(kN);
    fill(a, b);

    std::printf("\n[实验一] 逐元素乘法 c[i]=a[i]*b[i]，N=%zu（3 x %.0f MiB）\n",
                kN, static_cast<double>(kN * sizeof(float)) / (1024 * 1024));
    std::printf("  版本           ns/元素   相对标量   结果\n");

    const double s_scalar = bench_ns_per_elt(
        [&] { mul_scalar(a.data(), b.data(), c_scalar.data(), kN);
              return static_cast<double>(c_scalar[kN / 2]); }, kN);
    const double s_auto = bench_ns_per_elt(
        [&] { mul_auto(a.data(), b.data(), c_auto.data(), kN);
              return static_cast<double>(c_auto[kN / 2]); }, kN);
    const double s_simd = bench_ns_per_elt(
        [&] { mul_simd(a.data(), b.data(), c_simd.data(), kN);
              return static_cast<double>(c_simd[kN / 2]); }, kN);

    const bool auto_same = std::memcmp(c_auto.data(), c_scalar.data(),
                                       kN * sizeof(float)) == 0;
    const bool simd_same = std::memcmp(c_simd.data(), c_scalar.data(),
                                       kN * sizeof(float)) == 0;
    std::printf("  mul_scalar  %9.4f      1.00x   baseline\n", s_scalar);
    std::printf("  mul_auto    %9.4f      %5.2fx   %s\n", s_auto,
                s_scalar / s_auto, auto_same ? "逐位一致" : "不一致!");
    std::printf("  mul_simd    %9.4f      %5.2fx   %s\n", s_simd,
                s_scalar / s_simd, simd_same ? "逐位一致" : "不一致!");

    std::printf("\n[实验二] 点积 sum(a[i]*b[i])，N=%zu\n", kN);
    std::printf("  版本           ns/元素   相对标量   checksum\n");

    const float ref = dot_scalar(a.data(), b.data(), kN);
    struct Row { const char* name; float (*fn)(const float*, const float*, std::size_t); };
    const Row rows[] = {
        {"dot_scalar", dot_scalar}, {"dot_auto", dot_auto},
        {"dot_accum4", dot_accum4}, {"dot_simd1", dot_simd1},
        {"dot_simd4", dot_simd4},
    };
    double d_scalar = 0.0;
    for (const Row& row : rows) {
        const double ns = bench_ns_per_elt(
            [&] { return static_cast<double>(row.fn(a.data(), b.data(), kN)); }, kN);
        if (std::strcmp(row.name, "dot_scalar") == 0) d_scalar = ns;
        const float v = row.fn(a.data(), b.data(), kN);
        std::printf("  %-12s %9.4f      %5.2fx   %.9g (Δ=%+.6g)\n",
                    row.name, ns, d_scalar > 0.0 ? d_scalar / ns : 1.0,
                    static_cast<double>(v), static_cast<double>(v - ref));
    }
    std::printf("  baseline = dot_scalar = %.9g（Δ 为与该值的差）\n",
                static_cast<double>(ref));

    std::printf("\n[实验三] 工作集扫描：SIMD 加速比随容量衰减（mul_scalar / mul_auto / mul_simd）\n");
    std::printf("  N          工作集    scalar    auto      simd     auto倍率  simd倍率\n");
    const std::size_t sizes[] = {1u << 12, 1u << 16, 1u << 20, 1u << 24};
    for (std::size_t n : sizes) {
        std::vector<float> va(n), vb(n), v0(n), v1(n), v2(n);
        fill(va, vb);
        const double t0 = bench_ns_per_elt(
            [&] { mul_scalar(va.data(), vb.data(), v0.data(), n);
                  return static_cast<double>(v0[n / 2]); }, n);
        const double t1 = bench_ns_per_elt(
            [&] { mul_auto(va.data(), vb.data(), v1.data(), n);
                  return static_cast<double>(v1[n / 2]); }, n);
        const double t2 = bench_ns_per_elt(
            [&] { mul_simd(va.data(), vb.data(), v2.data(), n);
                  return static_cast<double>(v2[n / 2]); }, n);
        const double mib = static_cast<double>(3 * n * sizeof(float)) /
                           (1024 * 1024);
        std::printf("  %-9zu %7.2fMiB %8.4f %8.4f %8.4f %8.2fx %8.2fx\n",
                    n, mib, t0, t1, t2, t0 / t1, t0 / t2);
    }

    std::printf("\n提示：用 -S 生成汇编核对 mul_auto / dot_auto 是否真的出现向量指令。\n");
    return 0;
}
