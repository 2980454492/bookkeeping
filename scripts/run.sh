#!/bin/bash
# ═══════════════════════════════════════════════════════════════════
# 一键运行脚本（Linux / WSL）
# 构建 C++ 服务端 → 打开浏览器 → 启动 HTTP 服务
#
# 用法: ./scripts/run.sh
# ═══════════════════════════════════════════════════════════════════
set -euo pipefail

RED='\033[0;31m'; GREEN='\033[0;32m'; NC='\033[0m'
info()  { echo -e "${GREEN}==>${NC} $*"; }
error() { echo -e "${RED}❌${NC} $*"; }

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
SERVER_DIR="$PROJECT_DIR/server"
BUILD_DIR="$PROJECT_DIR/build"
PORT=18080

# ── 步骤0：环境检查 ──────────────────────────────────────────────
info "检查编译环境..."

TOOLS=(cmake g++)
MISSING=()
for tool in "${TOOLS[@]}"; do
    if ! command -v "$tool" &>/dev/null; then
        MISSING+=("$tool")
    fi
done
if [ ${#MISSING[@]} -gt 0 ]; then
    error "缺少: ${MISSING[*]}"
    echo ""
    echo "  安装命令: sudo apt install -y build-essential cmake libsqlite3-dev"
    echo ""
    exit 1
fi

# 检查源文件
if [ ! -f "$SERVER_DIR/CMakeLists.txt" ]; then
    error "未找到 $SERVER_DIR/CMakeLists.txt"
    echo "  请在项目根目录运行: ./scripts/run.sh"
    exit 1
fi
if [ ! -f "$SERVER_DIR/src/main.cpp" ]; then
    error "未找到源文件，请确认 server/src/ 目录完整"
    exit 1
fi
info "环境检查通过 ✓"

# ── 步骤1：CMake 配置 ────────────────────────────────────────────
info "CMake 配置..."

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

CMAKE_LOG=$(mktemp)
if ! cmake "$SERVER_DIR" -DCMAKE_BUILD_TYPE=Release > "$CMAKE_LOG" 2>&1; then
    error "CMake 配置失败:"
    echo ""
    cat "$CMAKE_LOG"
    echo ""
    echo "  常见原因:"
    echo "    • 缺少 libsqlite3-dev → sudo apt install libsqlite3-dev"
    echo "    • 缺少 zlib1g-dev      → sudo apt install zlib1g-dev"
    echo ""
    rm -f "$CMAKE_LOG"
    exit 1
fi
rm -f "$CMAKE_LOG"
info "CMake 配置完成 ✓"

# ── 步骤2：编译 ──────────────────────────────────────────────────
info "编译中..."
BUILD_LOG=$(mktemp)
if ! cmake --build . -j"$(nproc)" > "$BUILD_LOG" 2>&1; then
    error "编译失败:"
    echo ""
    grep -E "error:" "$BUILD_LOG" 2>/dev/null || cat "$BUILD_LOG"
    echo ""
    echo "  完整日志: $BUILD_LOG"
    exit 1
fi
rm -f "$BUILD_LOG"

EXE="$BUILD_DIR/bookkeeping"
if [ ! -f "$EXE" ]; then
    error "编译完成但未生成 bookkeeping 可执行文件"
    echo "  检查 CMake 输出是否有报错。"
    exit 1
fi
info "编译完成 ✓ ($(du -h "$EXE" | cut -f1))"

# ── 步骤3：打开浏览器（后台） ────────────────────────────────────
URL="http://127.0.0.1:$PORT"
(
    sleep 1.5
    if command -v wslview &>/dev/null; then
        wslview "$URL"
    elif command -v xdg-open &>/dev/null; then
        xdg-open "$URL"
    elif command -v open &>/dev/null; then
        open "$URL"
    else
        echo "👉 请手动打开浏览器访问: $URL"
    fi
) &
BROWSER_PID=$!

# ── 步骤4：启动服务 ──────────────────────────────────────────────
echo ""
echo "🚀 服务启动: $URL"
echo "   按 Ctrl+C 停止"
echo ""

trap "kill $BROWSER_PID 2>/dev/null; exit 0" EXIT
"$EXE"
