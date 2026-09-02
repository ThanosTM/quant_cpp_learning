// {{TITLE}} — 可运行示例（由 scripts/new_topic.sh 生成）
// 编译: g++ -std=c++20 -O2 -Wall example.cpp -o example && ./example
//
// 使用约定（与既有知识点 example 保持一致）：
//   1. 每个实验独立成函数，main 里按顺序运行并打印可观察的差异/结论；
//   2. 用输出说话：能打印计数、耗时、数值对比的，不要只靠注释；
//   3. 涉及性能的实验，先跑一遍 warmup，并注释说明方差控制方法。

#include <cstdint>
#include <iostream>

namespace {

// 实验一（示例骨架）：把这里替换为该知识点的核心演示。
void experiment_one() {
    std::cout << "[实验一] （说明本实验验证什么）\n";
    // TODO: 替换为真实演示代码
}

}  // namespace

int main() {
    std::cout << "== {{TITLE}} ==\n";
    experiment_one();
    return 0;
}
