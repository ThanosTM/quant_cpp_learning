# network 模块 — 计算机网络

> 低延迟岗位绕不开的模块：行情接入、订单网关都在 TCP/UDP 之上（[research/02](../../research/02-jd-core-system.md)）；面试常从「三次握手为什么是三次」一路追问到内核参数调优。

## 知识点清单

| 知识点 | 目录 | 状态 | 优先级 | 依据 |
|---|---|---|---|---|
| TCP 连接管理：三次握手 / 四次挥手、TIME_WAIT | tcp-handshake-teardown/ | planned | P0 | 富途真题（三次握手及底层、半连接/全连接队列）；HFT 题库 |
| TCP 可靠性：滑动窗口 / 拥塞控制 / 重传 / Nagle | tcp-reliability/ | planned | P0 | JD「网络原理」；面试必备 |
| IO 多路复用：select / poll / epoll、ET 与 LT | epoll-select-poll/ | planned | P0 | 垒昂面经；JD「网络编程」；高频手写题 |
| TCP 粘包与拆包：消息边界与 framing | tcp-framing/ | planned | P1 | 宽德 2026 实习二面真题；[research/03](../../research/03-tech-stack-map.md) |
| UDP 与行情组播（multicast） | udp-multicast/ | planned | P1 | 行情系统领域知识 |
| 零拷贝：sendfile / splice / mmap | zero-copy/ | planned | P1 | 与 low-latency 交叉 |
| 关键 socket 选项与内核调参：TCP_NODELAY / SO_REUSEADDR | socket-parameters/ | planned | P1 | 垒昂面经（TCP_NODELAY）；低延迟实战 |
| TLS/HTTPS 握手与延迟影响 | https-tls/ | planned | P2 | 外围系统了解即可 |

优先级定义见 [research/03-tech-stack-map.md](../../research/03-tech-stack-map.md)。

## 网络主线（建议学习顺序）

> 思路：先用抓包把 TCP 的连接与传输吃透（P0），再补 IO 模型与内核开关，最后进入数据通路与行情场景。每一站都要留下「可抓包 / 可计时」的实验证据，而不是只背结论。

| 顺序 | 知识点 | 优先级 | 这一站解决什么 | 实验锚点（example 选题） |
|---|---|---|---|---|
| 1 | [tcp-handshake-teardown](tcp-handshake-teardown/)（planned） | P0 | 连接建立/拆除的状态迁移；半连接与全连接队列、TIME_WAIT 的来源与追问链 | 最小 echo server/client + `tcpdump -i lo0` 抓握手/挥手；客户端主动关闭制造 TIME_WAIT 并用 `netstat` 观测；Linux 下缩小 backlog 观察队列溢出 |
| 2 | [tcp-reliability](tcp-reliability/)（planned） | P0 | 窗口/重传/拥塞控制如何决定吞吐与尾延迟；Nagle 的 40 ms 到底从哪来 | 缩小 `SO_RCVBUF` 观察发送阻塞；Linux `tc netem` 注入丢包后抓重传；开/关 `TCP_NODELAY` 对比小包往返时延 |
| 3 | [epoll-select-poll](epoll-select-poll/)（planned） | P0 | 单线程管理大量连接；LT/ET 语义与惊群；epoll 为什么是 O(1) 就绪分发 | Linux 容器内 1000 空闲连接 + 少量活跃连接，对比 select/poll/epoll 的 CPU 与延迟；复现 ET 未读完的坑 |
| 4 | [tcp-framing](tcp-framing/)（planned，新增） | P1 | 字节流没有消息边界：定长/分隔符/长度前缀三种 framing 的取舍与错误处理（宽德真题） | 构造半包、粘包、乱序包，三种 parser 对比吞吐与边界条件 |
| 5 | [socket-parameters](socket-parameters/)（planned） | P1 | 内核默认值为何坑低延迟：`SO_REUSEADDR`/`SO_LINGER`/`SO_RCVBUF`/`SO_SNDBUF` 的真实语义 | 复现 TIME_WAIT 下 bind 失败 vs 加 `SO_REUSEADDR`；`SO_LINGER=0` 发 RST；缓冲区大小对往返延迟的影响 |
| 6 | [zero-copy](zero-copy/)（planned） | P1 | 一次「读文件发网络」拷贝了几次；sendfile/splice/mmap 各自省掉哪一段 | 大文件发送 read+write vs sendfile vs mmap，比较吞吐与 `sys` 时间 |
| 7 | [udp-multicast](udp-multicast/)（planned） | P1 | 行情为什么用 UDP 组播；丢包/乱序/IGMP 与组播地址规划 | 组播 sender/receiver（回环）+ 序列号缺口统计；与 TCP 方案的延迟对比 |
| 8 | [https-tls](https-tls/)（planned） | P2 | TLS 1.2 vs 1.3 的握手 RTT 与延迟成本；何时在网关前终止 | 本地 TLS server + `openssl s_client` 测握手耗时；会话复用对照 |

- 顺序理由：1→2 是同一台状态机的前后两半（先会建连，再懂窗口/重传），Nagle 在 2 站一次讲透，避免 5 站只剩背结论；3 是 P0 但必须 Linux 环境，集中一次练完；4→5 把 TCP 知识落到「应用协议 + 内核开关」的工程层；6 与 [low-latency/kernel-bypass](../low-latency/README.md) 是同一条链的两端（内核零拷贝 → 内核旁路）；7 是行情系统场景题的地基；8 属 P2 补链，最后做。
- 与已展开知识点的衔接：3 站与 [context-switch-affinity](../os/context-switch-affinity/README.md) 联读（一次就绪唤醒的成本），并作为 os 模块 `syscall-vdso`、`interrupt-irq-affinity` 的前置；6 站与 [virtual-memory-tlb](../os/virtual-memory-tlb/README.md) 联读（mmap 的页表与缺页代价）。
- 真题联动：富途（三次握手底层、半连接/全连接）→ 1 站；垒昂（epoll ET/LT 惊群、`TCP_NODELAY`）→ 3/5 站；宽德 2026 实习二面（粘包拆包）→ 4 站。真题按题落 `interview/system/` 并与知识点双向互链。
- 每站验收（AGENT.md §2.A）：README 八节 + `example.cpp` 编译运行通过 + §5 写入本机实测/抓包结论 + ≥3 条追问 + 5 题自测 → `mastered`。
- 范围说明：本主线只覆盖岗位相关部分；IP/路由/DNS/HTTP 通识不单列，按 [research/03](../../research/03-tech-stack-map.md) 的优先级外挂补充。

## 本机实验环境注意（Apple M2 Pro / macOS）

- **epoll 不可用**：macOS 只有 kqueue，`epoll-select-poll` 的 example 必须 `#ifdef __linux__` 分支 + 在 Docker/远程 Linux 编译运行；本机只验证 select/poll 基线，正文标注「Linux 实测」。
- **零拷贝 API 差异**：`splice` 仅 Linux；macOS 的 `sendfile` 签名与 Linux 不同，example 需平台分支，README 给出两边对照。
- **丢包/重传实验**：依赖 Linux `tc netem`；macOS 的 `dnctl`/`pfctl` 组合成本高，退化为「小 `SO_RCVBUF` + 慢速读」观察窗口阻塞，重传结论在容器内补测。
- **队列观测**：半连接/全连接队列要 Linux `ss -lnt` + `netstat -s`；macOS 只有 `netstat -an`，相关数字不得冒充本机实测。
- **组播实验**：回环上需 `IP_MULTICAST_LOOP` + `IP_MULTICAST_IF` 才能收到；IGMP 与真实交换机行为另记，不混入本机结论。
- **抓包与信号**：`sudo tcpdump -i lo0 -nn -w /tmp/x.pcap`，用 Wireshark 分析；写已关闭连接会触发 `SIGPIPE`，example 统一 `signal(SIGPIPE, SIG_IGN)`（Linux 亦可 `MSG_NOSIGNAL`）；端口统一用 9000+ 避免 sudo。

## 模块说明

- 面试特征：国内面试网络题偏「背熟 + 追问原理」（握手挥手状态机、epoll 为什么快），笔试可能直接手写一个 echo server。
- 领域交叉：epoll → `os/` 的事件驱动；zero-copy、组播 → `low-latency/` 的 kernel-bypass 与行情架构。
- 典型追问链示例：三次握手 → 为什么不是两次（历史连接/SYN flood）→ TIME_WAIT 为什么 2MSL → 大量 TIME_WAIT 怎么办 → `SO_REUSEADDR` / `tcp_tw_reuse`。
