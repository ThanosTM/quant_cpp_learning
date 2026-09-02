# network 模块 — 计算机网络

> 低延迟岗位绕不开的模块：行情接入、订单网关都在 TCP/UDP 之上（[research/02](../../research/02-jd-core-system.md)）；面试常从「三次握手为什么是三次」一路追问到内核参数调优。

## 知识点清单

| 知识点 | 目录 | 状态 | 优先级 | 依据 |
|---|---|---|---|---|
| TCP 连接管理：三次握手 / 四次挥手、TIME_WAIT | tcp-handshake-teardown/ | planned | P0 | 面试必备；HFT 题库 |
| TCP 可靠性：滑动窗口 / 拥塞控制 / 重传 / Nagle | tcp-reliability/ | planned | P0 | 面试必备 |
| IO 多路复用：select / poll / epoll、ET 与 LT | epoll-select-poll/ | planned | P0 | JD「网络编程」；高频手写题 |
| UDP 与行情组播（multicast） | udp-multicast/ | planned | P1 | 行情系统领域知识 |
| 零拷贝：sendfile / splice / mmap | zero-copy/ | planned | P1 | 与 low-latency 交叉 |
| 关键 socket 选项与内核调参：TCP_NODELAY / SO_REUSEADDR | socket-parameters/ | planned | P1 | 低延迟实战 |
| TLS/HTTPS 握手与延迟影响 | https-tls/ | planned | P2 | 外围系统了解即可 |

优先级定义见 [research/03-tech-stack-map.md](../../research/03-tech-stack-map.md)。

## 模块说明

- 面试特征：国内面试网络题偏「背熟 + 追问原理」（握手挥手状态机、epoll 为什么快），笔试可能直接手写一个 echo server。
- 领域交叉：epoll → `os/` 的事件驱动；zero-copy、组播 → `low-latency/` 的 kernel-bypass 与行情架构。
- 典型追问链示例：三次握手 → 为什么不是两次（历史连接/SYN flood）→ TIME_WAIT 为什么 2MSL → 大量 TIME_WAIT 怎么办 → `SO_REUSEADDR` / `tcp_tw_reuse`。
