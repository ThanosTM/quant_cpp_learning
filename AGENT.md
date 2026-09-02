# AGENT.md — AI Agent 维护规范

> 本文件是 AI Agent 的自举入口。**每次会话开始时必须先读完本文件与根 README.md**，再执行任务。

## 0. 会话自举流程（每次必做）

1. 读根目录 `README.md`（项目导航）与本文件（维护规范）。
2. 读三级索引，确认当前状态：
   - `knowledge/README.md` —— 知识点总索引与学习进度
   - `interview/README.md` —— 真题与面经索引
   - `research/README.md` —— 岗位调研文档清单
3. 根据用户指令判断任务类型，进入 §2 对应工作流。
4. 全程使用**中文**输出；技术术语首次出现附英文原文。

## 1. 项目定位

- 目标岗位：量化私募 C++ 开发 / 核心系统开发（交易系统、行情系统等）。
- 内容三支柱：`research/`（调研）→ `knowledge/`（知识体系）→ `interview/`（真题实战）。
- 核心理念：**知识点 = 详细讲解 + 可运行 example + 追问沉淀**；真题必须按题给出分步解答；一切内容落盘为 Markdown，可 git 追溯。

## 2. 任务工作流

### A. 新增知识点（展开讲解）

1. 与用户确认知识点归属模块；若模块不存在则新建模块目录及 README。
2. 优先执行 `scripts/new_topic.sh <module> <slug> <中文标题>` 生成骨架（手动创建亦可，须遵循 `templates/knowledge-topic-README.md` 结构）。
3. 撰写正文：一句话定位 → 为什么面试官爱问 → 前置知识 → 核心讲解（分节）→ Example 说明 → 高频问答 → 深度追问记录 → 参考。
4. 编写 `example.cpp`（默认 `-std=c++20 -O2 -Wall`），**必须实际编译运行验证后**才算完成。
5. 更新两级索引：`knowledge/<module>/README.md` 与 `knowledge/README.md`（状态、日期）。
6. 追加 `CHANGELOG.md` 条目并 git commit。

### B. 追问落盘（深化知识点）—— 最常用的迭代方式

用户在学习某知识点时提出追问，Agent 回答后**必须立即落盘**：

1. **已有结论** → 追加到该知识点 `README.md` 的「§7 深度追问记录」表格：日期 / 问题 / 结论摘要 / 联动更新。
2. 若结论重要到影响正文理解 → **同步更新正文对应小节**，并在「联动更新」列注明章节号。
3. **尚无定论或需查证** → 记入该知识点的 `followups.md`（从 `templates/followups.md` 复制），状态标 `待查证 / 进行中`，后续补齐后转正。
4. 需要代码佐证 → 更新或新增 example 文件，**重新编译验证**。
5. 更新该知识点「更新」日期；追问累计 ≥3 条且自测通过时，可将状态升级为 `mastered`。

### C. 新增真题

1. 按 `templates/question.md` 创建：`interview/<类别>/<序号>-<简短英文题名>.md`，序号三位递增（如 `cpp/002-move-vs-copy.cpp.md` 类命名：`002-short-slug.md`）。
2. **按题给出分步解答**：结论先行 → 推理步骤 → 代码验证（如适用）→ 追问链 → 关联知识点链接。
3. 注明来源：公司/轮次/年份 + 链接（用户提供则标「用户提供」，检索所得须带检索日期）。
4. 更新 `interview/README.md` 索引与 `CHANGELOG.md`。

### D. 面经归档

1. 按 `templates/experience.md` 归档到 `interview/experiences/<NNN>-<公司>-<年份>.md`。
2. 拆解出可复用的真题时，转入工作流 C 单独建题，并在面经中注明「已拆解 → 题号」。

### E. 调研刷新

1. 检索最新 JD 与面经（中文为主、英文补充），更新 `research/` 对应文档。
2. **每篇调研文档头部维护「最后核实日期」**；过时内容不删除，标注日期后归入「历史观察」。
3. 若发现新考点 → 同步到 `research/03-tech-stack-map.md` 并在对应模块 README 的规划表中登记。

### F. 状态管理

- 用户说「掌握了某知识点」→ 状态升级 `mastered`，并在该知识点 README 末尾追加自测清单（5 题快问快答）。
- 用户要求复习 → 根据「深度追问记录」生成复习卡片。

## 3. 目录与命名规范

| 项 | 规范 |
|---|---|
| 知识点目录 | `knowledge/<module>/<kebab-case-slug>/`，内含 `README.md`（讲解）、`example.cpp`、可选 `followups.md`、可选 `example2*.cpp` |
| 真题文件 | `interview/<category>/NNN-short-slug.md`，NNN 三位序号，category ∈ cpp/system/algorithm/brainteaser |
| 面经文件 | `interview/experiences/NNN-<company>-<year>.md` |
| 状态枚举 | `planned`（仅规划）/ `outline`（仅大纲）/ `expanded`（正文+example 完成）/ `mastered`（追问沉淀+自测通过） |
| 深度等级 | L1 概念 / L2 原理+代码 / L3 生产级权衡（由追问沉淀产生） |
| 日期格式 | `YYYY-MM-DD` |

## 4. 写作与代码质量门槛

- 全中文撰写；讲解分节固定（见模板），先动机后机制，避免罗列 API。
- `example.cpp` 必须给出编译命令注释，且**落盘前实际编译运行通过**；默认编译参数 `-std=c++20 -O2 -Wall`。
- 真题解答必须「按题」逐步展开，禁止只给要点不展开；代码验证块需可直接复制编译。
- 外部信息必须附来源链接与核实日期；**禁止编造面经、公司名、题目**；无法核实的标注 `[未核实]`。
- 知识点之间用相对路径互相链接（如 `../move-semantics/README.md`），与真题互链形成网状结构。

## 5. 索引一致性义务（三级索引）

任何内容变更后，同步检查：
1. 知识点所属模块 `README.md` 的清单表（状态、日期）；
2. `knowledge/README.md` 总索引；
3. `interview/README.md`（涉及真题/面经时）；
4. `README.md` 顶部进度表（已展开/进行中计数变化时）；
5. `CHANGELOG.md` 追加条目。

## 6. Git 提交规范

- 提交信息：`<type>: <摘要>`，type ∈ feat（新内容）/ fix（修正）/ docs（文档）/ chore（杂务）。
- 建议每次完成一个工作流即提交一次，保证可回溯。

## 7. 禁止事项

- 禁止在未编译验证的情况下声称 example 可运行。
- 禁止编造或模糊化处理真题来源。
- 禁止跳过索引更新（§5）。
- 禁止删除用户的追问记录——只能追加或标注已解决。
