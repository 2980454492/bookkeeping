#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════
# check-docs.sh — 校验 .md 文档与代码是否同步
#
# 用法:
#   ./scripts/check-docs.sh           # 只检查，有差异时报错
#   ./scripts/check-docs.sh --quiet   # 无问题时不输出（适合 hook）
#
# 检查项:
#   1. scripts/ 下脚本是否都在 README 项目树中列出
#   2. server/src/*.cpp 是否都在 CMakeLists.txt SOURCES 中
#   3. handlers.cpp 注册的 API 路由是否都在 README API 表中
#   4. 端口号在 README / run.sh / run.bat / main.cpp 中是否一致
#   5. .claude/skills/ 下 skill 是否都在 settings.json 注册
#
# 设计决策:
#   - 只检查可自动检测的项（不替代人工 review）
#   - 不检查 requirements.md（功能增删需人工判断）
#   - 不检查编码规范变更（需人工判断）
# ═══════════════════════════════════════════════════════════════════

set -euo pipefail

# ── 颜色 ──────────────────────────────────────────────────────────
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# ── 初始化 ────────────────────────────────────────────────────────
QUIET=false
[[ "${1:-}" == "--quiet" ]] && QUIET=true

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

FAILURES=0
WARNINGS=0

info()  { $QUIET || echo -e "${CYAN}[检查]${NC} $*"; }
pass()  { echo -e "  ${GREEN}✓${NC} $*"; }
warn()  { echo -e "  ${YELLOW}⚠${NC} $*"; WARNINGS=$((WARNINGS + 1)); }
fail()  { echo -e "  ${RED}✗${NC} $*"; FAILURES=$((FAILURES + 1)); }

# ── 检查 1：scripts/ 脚本 → README 项目树 ────────────────────────
info "1. scripts/ 目录 → README.md 项目结构图"

for script in scripts/*; do
    # 跳过目录和非脚本文件
    [[ -f "$script" ]] || continue
    script_name="$(basename "$script")"
    # 跳过 .cmake 文件（工具链配置，不是脚本）
    if grep -q "$script_name" README.md 2>/dev/null; then
        pass "$script_name 已在 README 中列出"
    else
        fail "$script_name 未在 README.md 项目结构图中找到"
    fi
done

# ── 检查 2：C++ 源文件 → CMakeLists.txt ──────────────────────────
info "2. server/src/*.cpp → server/CMakeLists.txt SOURCES"

for src in server/src/*.cpp; do
    src_rel="src/$(basename "$src")"
    if grep -qF "\"$src_rel\"" server/CMakeLists.txt 2>/dev/null || \
       grep -qF "$src_rel" server/CMakeLists.txt 2>/dev/null; then
        pass "$(basename "$src") 已在 CMakeLists.txt SOURCES 中"
    else
        fail "$(basename "$src") 未在 CMakeLists.txt SOURCES 中找到（添加后编译才会包含此文件）"
    fi
done

# ── 检查 3：API 路由 → README API 表 ─────────────────────────────
info "3. handlers.cpp API 路由 → README.md API 表格"

# 提取 handlers.cpp 中注册的路由（非注释行）
ROUTES=$(grep -n 'svr\.\(Get\|Post\|Put\|Delete\|Patch\)' server/src/handlers.cpp \
    | grep -v '^\s*//' \
    | sed -n 's/.*svr\.\(Get\|Post\|Put\|Delete\|Patch\)(\s*\(R"(\|"\)\([^"()]*\).*/\1 \3/p' \
    || true)

if [[ -z "$ROUTES" ]]; then
    warn "未能从 handlers.cpp 解析到路由（grep 模式可能需要调整）"
else
    while IFS= read -r line; do
        method="${line%% *}"
        path="${line#* }"
        # 将正则路由标准化：/api/records/(\d+) → /api/records/:id
        normalized_path=$(echo "$path" | sed 's/\\//g' | sed 's/(\\d+)/:id/g')

        # 在 README.md API 表格中搜索（表格格式：| METHOD | /path |）
        if grep -qE "^\|\s*${method}\s*\|\s*/api/${normalized_path#/api/}" README.md 2>/dev/null; then
            pass "${method} ${normalized_path}"
        elif grep -qF "$normalized_path" README.md 2>/dev/null; then
            pass "${method} ${normalized_path} (路径已找到)"
        else
            fail "${method} ${normalized_path} 未在 README.md API 表格中找到"
        fi
    done <<< "$ROUTES"
fi

# ── 检查 4：端口号一致性 ──────────────────────────────────────────
info "4. 端口号一致性（README / run.sh / run.bat / main.cpp）"

# 提取各文件中的端口号（取第一个匹配的数字 18080）
PORT_README=$(grep -oP '127\.0\.0\.1:\K[0-9]+' README.md | head -1 || echo "未找到")
PORT_RUN_SH=$(grep -oP '^PORT=\K[0-9]+' scripts/run.sh || echo "未找到")
PORT_RUN_BAT=$(grep -oP 'set "PORT=\K[0-9]+' scripts/run.bat || echo "未找到")
PORT_MAIN_CPP=$(grep -oP 'int port = \K[0-9]+' server/src/main.cpp | head -1 || echo "未找到")

# 收集所有找到的端口
declare -A PORT_SOURCES
PORT_SOURCES["README.md"]="$PORT_README"
PORT_SOURCES["scripts/run.sh"]="$PORT_RUN_SH"
PORT_SOURCES["scripts/run.bat"]="$PORT_RUN_BAT"
PORT_SOURCES["server/src/main.cpp"]="$PORT_MAIN_CPP"

# 确定参考端口（取 main.cpp 中的，因为它是代码真理源）
REF_PORT="$PORT_MAIN_CPP"

if [[ "$REF_PORT" == "未找到" ]]; then
    warn "无法从 main.cpp 提取端口号，跳过一致性检查"
else
    for src in "${!PORT_SOURCES[@]}"; do
        val="${PORT_SOURCES[$src]}"
        if [[ "$val" == "未找到" ]]; then
            warn "$src 中未找到端口号定义"
        elif [[ "$val" == "$REF_PORT" ]]; then
            pass "$src → $val"
        else
            fail "$src 端口为 $val，与 main.cpp 中的 $REF_PORT 不一致"
        fi
    done
fi

# ── 检查 5：skill 目录 → settings.json 注册 ──────────────────────
info "5. .claude/skills/ → .claude/settings.json 注册"

# 从 settings.json 提取已注册的 skill 名称
REGISTERED_SKILLS=$(python3 -c "
import json, sys
try:
    with open('.claude/settings.json') as f:
        data = json.load(f)
    for s in data.get('skills', []):
        print(s.get('name', ''))
except Exception as e:
    print(f'ERROR:{e}', file=sys.stderr)
    sys.exit(1)
" 2>/dev/null || echo "")

if [[ -z "$REGISTERED_SKILLS" ]]; then
    warn "无法解析 settings.json，跳过 skill 注册检查"
else
    for skill_dir in .claude/skills/*/; do
        skill_name="$(basename "$skill_dir")"
        if echo "$REGISTERED_SKILLS" | grep -qFx "$skill_name"; then
            pass "$skill_name 已在 settings.json 注册"
        else
            fail "$skill_name 目录存在但未在 settings.json skills 中注册"
        fi
    done
fi

# ── 结果 ──────────────────────────────────────────────────────────
echo ""
if [[ $FAILURES -eq 0 ]] && [[ $WARNINGS -eq 0 ]]; then
    echo -e "${GREEN}══════════════════════════════════════════════${NC}"
    echo -e "${GREEN}  全部检查通过 ✓${NC}"
    echo -e "${GREEN}══════════════════════════════════════════════${NC}"
    exit 0
elif [[ $FAILURES -eq 0 ]]; then
    echo -e "${YELLOW}══════════════════════════════════════════════${NC}"
    echo -e "${YELLOW}  通过（${WARNINGS} 个警告）${NC}"
    echo -e "${YELLOW}══════════════════════════════════════════════${NC}"
    exit 0
else
    echo -e "${RED}══════════════════════════════════════════════${NC}"
    echo -e "${RED}  ${FAILURES} 项失败，${WARNINGS} 个警告${NC}"
    echo -e "${RED}══════════════════════════════════════════════${NC}"
    echo ""
    echo "修复提示："
    echo "  - 文件/脚本未列出 → 更新 README.md 项目结构图"
    echo "  - API 路由未列出    → 更新 README.md API 表格"
    echo "  - .cpp 未在 SOURCES → 更新 server/CMakeLists.txt"
    echo "  - 端口不一致        → 确保 README / run.sh / run.bat 与 main.cpp 一致"
    echo "  - skill 未注册       → 更新 .claude/settings.json skills 列表"
    exit 1
fi
