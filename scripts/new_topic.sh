#!/usr/bin/env bash
# new_topic.sh — 新建一个知识点目录（README + example.cpp + followups.md）
#
# 用法:
#   ./scripts/new_topic.sh <module> <kebab-slug> [标题]
# 示例:
#   ./scripts/new_topic.sh cpp stl-containers "STL 容器底层"
#   ./scripts/new_topic.sh low-latency memory-ordering
#
# 模块取值: cpp | low-latency | network | os | arch | brainteaser
# 说明: 脚本只负责目录与模板落地；索引更新（模块 README 清单、
#       knowledge/README.md 进度表、根 README 进度、CHANGELOG）由 Agent 按 AGENT.md §5 完成。

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TEMPLATE_DIR="${REPO_ROOT}/templates"
KNOWLEDGE_DIR="${REPO_ROOT}/knowledge"

usage() {
    echo "用法: $0 <module> <kebab-slug> [标题]"
    echo "模块: cpp | low-latency | network | os | arch | brainteaser"
    exit 1
}

[[ $# -ge 2 ]] || usage
MODULE="$1"
SLUG="$2"
TITLE="${3:-}"
DATE="$(date +%F)"

case "${MODULE}" in
    cpp|low-latency|network|os|arch|brainteaser) ;;
    *) echo "错误: 未知模块 '${MODULE}'（允许: cpp low-latency network os arch brainteaser）"; exit 1 ;;
esac

[[ "${SLUG}" =~ ^[a-z0-9]+(-[a-z0-9]+)*$ ]] || {
    echo "错误: slug 必须是 kebab-case（小写字母/数字，连字符分隔），得到 '${SLUG}'"; exit 1
}

TARGET_DIR="${KNOWLEDGE_DIR}/${MODULE}/${SLUG}"
[[ -e "${TARGET_DIR}" ]] && { echo "错误: 目录已存在 ${TARGET_DIR}"; exit 1; }

if [[ -z "${TITLE}" ]]; then
    echo "提示: 未提供标题，README 中 {{TITLE}} 将保持为占位符，请手动填写。"
    TITLE="{{TITLE}}"
fi

mkdir -p "${TARGET_DIR}"

# 用「生成新文件」的方式填充模板，兼容 macOS(BSD sed) 与 GNU sed（避免 -i 参数差异）
render() {
    local src="$1" dst="$2"
    sed -e "s|{{TITLE}}|${TITLE}|g" \
        -e "s|{{MODULE}}|${MODULE}|g" \
        -e "s|{{SLUG}}|${SLUG}|g" \
        -e "s|{{DATE}}|${DATE}|g" \
        "${src}" > "${dst}"
}

render "${TEMPLATE_DIR}/knowledge-topic-README.md" "${TARGET_DIR}/README.md"
render "${TEMPLATE_DIR}/example.cpp"              "${TARGET_DIR}/example.cpp"
render "${TEMPLATE_DIR}/followups.md"             "${TARGET_DIR}/followups.md"

echo "已创建知识点:"
echo "  ${TARGET_DIR}/README.md"
echo "  ${TARGET_DIR}/example.cpp"
echo "  ${TARGET_DIR}/followups.md"
echo
echo "后续步骤（Agent 执行 AGENT.md §2.A）:"
echo "  1. 编写 README 核心讲解与 example；编译验证: g++ -std=c++20 -O2 -Wall ${TARGET_DIR}/example.cpp -o /tmp/example"
echo "  2. 在 knowledge/${MODULE}/README.md 知识点清单中登记新目录并更新状态"
echo "  3. 更新 knowledge/README.md 学习进度表（及根 README 进度、CHANGELOG）"
