#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"

# ── 步骤1：编译构建 ──────────────────────────────────────────────
echo "🔨 正在编译..."
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake "$SCRIPT_DIR" -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
echo "✅ 编译完成。"
echo ""

# ── 步骤2：打开浏览器（后台执行，延迟启动） ──────────────────────
(
    sleep 1.5
    URL="http://127.0.0.1:18080"
    if command -v wslview &>/dev/null; then
        wslview "$URL"          # WSL 环境
    elif command -v xdg-open &>/dev/null; then
        xdg-open "$URL"         # Linux 桌面
    elif command -v open &>/dev/null; then
        open "$URL"             # macOS
    else
        echo "👉 请手动打开浏览器访问: $URL"
    fi
) &
BROWSER_PID=$!

# ── 步骤3：启动服务器（前台运行） ─────────────────────────────────
echo "🚀 正在启动服务器..."
echo "   http://127.0.0.1:18080"
echo "   按 Ctrl+C 停止"
echo ""

trap "kill $BROWSER_PID 2>/dev/null; exit 0" EXIT
./bookkeeping
