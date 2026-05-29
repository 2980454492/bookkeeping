#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

# ── Step 1: Build ───────────────────────────────────────────────
echo "🔨 Building..."
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
echo "✅ Build complete."
echo ""

# ── Step 2: Open browser (background, slight delay) ─────────────
(
    sleep 1.5
    URL="http://127.0.0.1:18080"
    if command -v wslview &>/dev/null; then
        wslview "$URL"          # WSL
    elif command -v xdg-open &>/dev/null; then
        xdg-open "$URL"         # Linux
    elif command -v open &>/dev/null; then
        open "$URL"             # macOS
    else
        echo "👉 请手动打开浏览器访问: $URL"
    fi
) &
BROWSER_PID=$!

# ── Step 3: Start server (runs in foreground) ──────────────────
echo "🚀 Starting server..."
echo "   Open http://127.0.0.1:18080 in your browser"
echo "   Press Ctrl+C to stop"
echo ""

trap "kill $BROWSER_PID 2>/dev/null; exit 0" EXIT
./bookkeeping
