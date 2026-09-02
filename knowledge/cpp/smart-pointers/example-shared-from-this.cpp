// 追问四：enable_shared_from_this 的使用示例与三个坑
// 编译: g++ -std=c++20 -O2 -Wall example-shared-from-this.cpp -o shared_from_this && ./shared_from_this
//
// 结论速览：
//   - 用途：对象在【自己的成员函数内部】需要把"管理自身生命的 shared_ptr"交给外部
//     （异步回调、注册到事件表、任务队列），必须用 shared_from_this()
//   - 机制：基类 enable_shared_from_this<T> 在对象里内嵌一个 weak_ptr<T>；
//     第一个 shared_ptr 构造时把它指向自己的控制块；shared_from_this() 就是
//     对这个内嵌 weak 做 lock()（所以强计数会 +1，且要求对象已被 shared_ptr 管理）
//   - 坑一：对象没有被 shared_ptr 管理（栈对象/裸 new）→ 抛 std::bad_weak_ptr
//   - 坑二：在构造函数里调用 → 内嵌 weak 还没被初始化 → 同样 bad_weak_ptr
//     （正确替代：两段式 init()，或工厂函数构造完成后再注册）
//   - 坑三：std::shared_ptr<T>(this) → 为同一对象造第二个控制块 → 双重释放
//     （鉴别两个 shared_ptr 是否同块：owner_before 双向皆 false 才是同一块；
//      operator== 只比较裸指针，识别不了双控制块！）
//   - 代价：内嵌 weak 恒占 16 字节（两个指针），即使从不使用；C++17 提供
//     noexcept 的 weak_from_this() 直接拿内嵌 weak，不抛异常

#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

// ---- 示例类：异步任务，把自己的生命周期交给后台线程 ----
struct Job : std::enable_shared_from_this<Job> {
    std::string name;
    explicit Job(std::string n) : name(std::move(n)) {}
    ~Job() { std::cout << "  ~Job " << name << "\n"; }

    // 正确姿势: shared_from_this() 拷贝一份"管理自己的 shared_ptr"进回调,
    // 主线程即使提前丢掉 Job, 回调执行期间对象也不会析构
    std::thread run_in_background() {
        auto self = shared_from_this();  // 强计数 +1, 与外部共享同一控制块
        return std::thread([self] {
            std::this_thread::sleep_for(std::chrono::milliseconds(30));  // 模拟异步工作
            std::cout << "  [后台线程] 执行 " << self->name
                      << ", use_count=" << self.use_count() << "（j1 + j2 + 回调 self 各持 1）\n";
        });
    }
};

// ---- 用于坑二: 构造函数内调用 ----
struct Early : std::enable_shared_from_this<Early> {
    Early() {
        try {
            auto sp = shared_from_this();  // 此刻还没有任何 shared_ptr 拥有本对象
            (void)sp;
            std::cout << "  （不可能到达）\n";
        } catch (const std::bad_weak_ptr&) {
            std::cout << "  构造函数内调用 → 捕获 bad_weak_ptr（内嵌 weak 尚未初始化）\n";
        }
    }
};

namespace {

// ========== 实验一：正确用法 + weak_from_this ==========
void experiment_one() {
    std::cout << "[实验一] shared_from_this(): 与外部共享同一控制块\n";
    auto j1 = std::make_shared<Job>("编译任务");
    std::shared_ptr<Job> j2 = j1->shared_from_this();
    std::cout << "  j1.use_count=" << j1.use_count()
              << ", j2.use_count=" << j2.use_count() << "（+1 自 shared_from_this）\n";
    std::cout << "  同一控制块? " << std::boolalpha << !(j1.owner_before(j2) || j2.owner_before(j1)) << "\n";

    std::weak_ptr<Job> wk = j1->weak_from_this();  // C++17: noexcept, 不增计数
    std::cout << "  weak_from_this(): expired=" << wk.expired() << "\n";

    std::cout << "  → 交给后台线程, 主线程 join 等待:\n";
    auto t = j1->run_in_background();
    t.join();  // join 后回调已结束, j1/j2 仍在
}  // 作用域结束: j2 先析构(-1), j1 再析构(-1→0) → ~Job

// ========== 实验二：坑一 —— 对象未被 shared_ptr 管理 ==========
void experiment_two() {
    std::cout << "[实验二] 坑一: 栈对象/裸 new 对象调用 shared_from_this()\n";
    Job stack_job("栈对象");
    try {
        auto sp = stack_job.shared_from_this();
        (void)sp;
    } catch (const std::bad_weak_ptr& e) {
        std::cout << "  捕获 bad_weak_ptr: " << e.what() << "\n";
    }
    std::cout << "  → 用法前提: 对象必须已经由 shared_ptr 接管\n";
}  // stack_job 正常析构

// ========== 实验三：坑二 —— 构造函数内调用 ==========
void experiment_three() {
    std::cout << "[实验三] 坑二: 构造函数内调用 shared_from_this()\n";
    auto e = std::make_shared<Early>();  // ctor 内部 catch, 对象正常构造完成
    (void)e;
    std::cout << "  → 替代方案: 两段式 init(), 或工厂函数中构造完成后再注册回调\n";
}

// ========== 实验四：坑三 —— shared_ptr<T>(this) 造第二个控制块 ==========
void experiment_four() {
    std::cout << "[实验四] 坑三: shared_ptr<T>(this) → 两个控制块(只看证据, 不真触发 abort)\n";
    struct Dummy { int v = 1; };
    Dummy* raw = new Dummy;
    std::shared_ptr<Dummy> p1(raw);  // 正常路径: 控制块 #1
    // 错误路径(此处借一个故意泄漏的指针观察, 避免真正双重释放 abort):
    //   std::shared_ptr<Dummy> p2(raw);  ← 造出控制块 #2
    auto* p2 = new std::shared_ptr<Dummy>(raw);  // 故意不析构, 防止演示双重释放

    std::cout << "  p1.use_count=" << p1.use_count()
              << ", p2.use_count=" << p2->use_count() << "（同一对象, 两块各计各的）\n";
    std::cout << "  p1 == p2 ? " << std::boolalpha << (p1 == *p2)
              << "  ← operator== 只比裸指针, 看不出双块!\n";
    bool same_block = !(p1.owner_before(*p2) || p2->owner_before(p1));
    std::cout << "  同一控制块? " << same_block
              << "  ← owner_before 双向皆 false 才是同块\n";
    std::cout << "  若让 p1/p2 都正常析构 → 对同一 raw 两次 delete → double free abort\n";
    // p1 析构(函数尾): delete raw + 释放控制块 #1; p2 所指永久泄漏 → 无崩溃
}

// ========== 实验五：代价 —— 内嵌 weak 的 16 字节 ==========
void experiment_five() {
    std::cout << "[实验五] 代价: 基类内嵌 weak_ptr 恒占 16 字节\n";
    std::cout << "  sizeof(std::enable_shared_from_this<int>) = "
              << sizeof(std::enable_shared_from_this<int>) << "（两个指针: 对象指针+控制块指针）\n";
    static_assert(sizeof(std::enable_shared_from_this<int>) == 2 * sizeof(void*));
    std::cout << "  → 即使从不调用 shared_from_this(), 继承它就付这 16 字节\n";
}

}  // namespace

int main() {
    std::cout << "== 追问四: enable_shared_from_this ==\n";
    experiment_one();
    experiment_two();
    experiment_three();
    experiment_four();
    experiment_five();
    std::cout << "\n要点复述:\n"
              << "  1. 成员函数内要交出'管理自身的 shared_ptr' → 只能用 shared_from_this()\n"
              << "  2. 前提: 对象已被 shared_ptr 管理, 且构造已完成(否则 bad_weak_ptr)\n"
              << "  3. shared_ptr(this) = 第二控制块 = 双重释放; owner_before 可鉴别同块\n"
              << "  4. 低延迟提醒: 热路径对象不需要它就别继承, 省下恒定的 16 字节\n";
    return 0;
}
