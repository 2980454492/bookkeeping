#!/bin/bash
# ═══════════════════════════════════════════════════════════════════
# WSL → Windows 交叉编译脚本
# 在 WSL/Linux 中生成 bookkeeping-server.exe（无需 Visual Studio）
#
# 用法:
#   ./scripts/build-win.sh          # 正常编译
#   ./scripts/build-win.sh clean    # 清除构建缓存后编译
# ═══════════════════════════════════════════════════════════════════
set -euo pipefail

# ── 颜色输出 ──────────────────────────────────────────────────────
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BOLD='\033[1m'
NC='\033[0m'

info()    { echo -e "${GREEN}==>${NC} $*"; }
warn()    { echo -e "${YELLOW}⚠️  $*${NC}"; }
error()   { echo -e "${RED}❌ $*${NC}"; }
section() { echo ""; echo -e "${BOLD}── $* ──${NC}"; echo ""; }
die()     { error "$@"; exit 1; }

# ── 路径常量 ──────────────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
# 安全校验：确保 PROJECT_DIR 合法
if [ -z "$PROJECT_DIR" ] || [ "$PROJECT_DIR" = "/" ]; then
    die "无法确定项目根目录（SCRIPT_DIR=$SCRIPT_DIR）"
fi

SERVER_DIR="$PROJECT_DIR/server"
BUILD_DIR="$PROJECT_DIR/build-mingw"
DEPS_DIR="$BUILD_DIR/_deps"
DIST_DIR="$PROJECT_DIR/dist/Bookkeeping"
CROSS_PREFIX="x86_64-w64-mingw32"

CROSS_GCC="${CROSS_PREFIX}-gcc"
CROSS_GXX="${CROSS_PREFIX}-g++"
CROSS_AR="${CROSS_PREFIX}-ar"
CROSS_OBJDUMP="${CROSS_PREFIX}-objdump"

# ── 清理模式 ──────────────────────────────────────────────────────
if [ "${1:-}" = "clean" ]; then
    info "清理构建缓存和发布目录..."
    rm -rf "$BUILD_DIR" "$DIST_DIR"
    info "清理完成，继续编译..."
fi

# ═══════════════════════════════════════════════════════════════════
# 步骤1：环境检查
# ═══════════════════════════════════════════════════════════════════
section "[1/6] 检查编译环境"

# 1a. 交叉编译器
if ! command -v "$CROSS_GXX" &>/dev/null; then
    die "未找到 mingw-w64 交叉编译器 ($CROSS_GXX)

  请运行以下命令安装：

    sudo apt update
    sudo apt install -y g++-mingw-w64-x86-64-posix

  安装完成后重新运行此脚本。"
fi
info "交叉编译器: $($CROSS_GXX --version | head -1)"

# 1b. 基础工具
MISSING=()
for tool in cmake python3 curl; do
    if ! command -v "$tool" &>/dev/null; then MISSING+=("$tool"); fi
done
if [ ${#MISSING[@]} -gt 0 ]; then
    die "缺少以下工具: ${MISSING[*]}

  请运行: sudo apt install -y ${MISSING[*]}"
fi
info "CMake:      $(cmake --version | head -1)"
info "Python3:    $(python3 --version)"

# 1c. rsync（可选，无则用 cp）
if command -v rsync &>/dev/null; then
    info "rsync:      已安装"
else
    warn "rsync 未安装，将使用 cp 复制前端文件（跳过排除规则）"
fi

# 1d. objdump（用于检查 DLL 依赖，可选）
if ! command -v "$CROSS_OBJDUMP" &>/dev/null; then
    warn "$CROSS_OBJDUMP 未安装，编译后无法检查 DLL 依赖"
    warn "安装: sudo apt install -y binutils-mingw-w64-x86-64"
    HAS_OBJDUMP=false
else
    HAS_OBJDUMP=true
fi

# 1e. 磁盘空间检查（至少需要 500MB）
AVAIL_MB=$(df -BM "$PROJECT_DIR" 2>/dev/null | awk 'NR==2{print $4}' | sed 's/M//')
if [ -n "$AVAIL_MB" ] && [ "$AVAIL_MB" -lt 500 ] 2>/dev/null; then
    warn "磁盘可用空间不足 500MB（当前: ${AVAIL_MB}MB），编译可能失败"
fi

# 1f. 检查项目源文件
REQUIRED_FILES=(
    "$SERVER_DIR/src/main.cpp"
    "$SERVER_DIR/src/db.cpp"
    "$SERVER_DIR/src/handlers.cpp"
    "$SERVER_DIR/src/export_util.cpp"
    "$SERVER_DIR/src/import_util.cpp"
    "$SERVER_DIR/vendor/httplib.h"
    "$SERVER_DIR/vendor/json.hpp"
    "$SERVER_DIR/CMakeLists.txt"
    "$SERVER_DIR/frontend/index.html"
    "$SERVER_DIR/categories.json"
)
MISSING_FILES=()
for f in "${REQUIRED_FILES[@]}"; do
    [ -f "$f" ] || MISSING_FILES+=("$f")
done
if [ ${#MISSING_FILES[@]} -gt 0 ]; then
    error "缺少以下项目源文件:"
    for f in "${MISSING_FILES[@]}"; do echo "     - $f"; done
    echo ""
    echo "  httplib.h 和 json.hpp 需要手动下载:"
    echo ""
    echo "    mkdir -p server/vendor"
    echo "    curl -L 'https://cdn.jsdelivr.net/gh/yhirose/cpp-httplib@v0.16.3/httplib.h' -o server/vendor/httplib.h"
    echo "    curl -L 'https://cdn.jsdelivr.net/gh/nlohmann/json@v3.11.3/single_include/nlohmann/json.hpp' -o server/vendor/json.hpp"
    echo ""
    exit 1
fi
info "项目源文件完整 ✓"

# ═══════════════════════════════════════════════════════════════════
# 步骤2：构建 SQLite3 Windows 静态库
# ═══════════════════════════════════════════════════════════════════
section "[2/6] 构建 SQLite3 for Windows"

SQLITE_VER="3450300"
SQLITE_YEAR="2024"
SQLITE_DIR="$DEPS_DIR/sqlite-amalgamation-$SQLITE_VER"
SQLITE_ZIP="sqlite-amalgamation-$SQLITE_VER.zip"
SQLITE_URL="https://www.sqlite.org/$SQLITE_YEAR/$SQLITE_ZIP"

if [ ! -f "$SQLITE_DIR/.built" ]; then
    mkdir -p "$DEPS_DIR"

    # 下载
    if [ ! -f "$SQLITE_DIR/sqlite3.c" ]; then
        cd "$DEPS_DIR"

        if [ ! -f "$SQLITE_ZIP" ]; then
            info "下载 SQLite amalgamation..."
            if ! curl -fSL --retry 3 --connect-timeout 10 \
                "$SQLITE_URL" -o "$SQLITE_ZIP"; then
                error "下载失败: $SQLITE_URL"
                echo ""
                echo "  手动解决办法:"
                echo "    1. 浏览器打开 https://www.sqlite.org/download.html"
                echo "    2. 搜索 'sqlite-amalgamation'，下载最新 zip"
                echo "    3. 放入 $DEPS_DIR/"
                echo "    4. 重新运行此脚本"
                echo ""
                rm -f "$SQLITE_ZIP"
                exit 1
            fi
            info "下载完成 ($(du -h "$SQLITE_ZIP" | cut -f1))"
        fi

        info "解压..."
        if ! python3 -c "
import zipfile, sys, os
try:
    with zipfile.ZipFile('$SQLITE_ZIP') as zf:
        # 验证 zip 完整性
        bad = zf.testzip()
        if bad:
            print(f'zip 文件损坏: {bad}', file=sys.stderr)
            sys.exit(1)
        zf.extractall()
    print('解压成功')
except zipfile.BadZipFile as e:
    print(f'不是有效的 zip 文件: {e}', file=sys.stderr)
    sys.exit(1)
except Exception as e:
    print(f'解压失败: {e}', file=sys.stderr)
    sys.exit(1)
"; then
            error "解压 $SQLITE_ZIP 失败，文件可能损坏"
            echo ""
            echo "  删除损坏文件后重试:"
            echo "    rm $DEPS_DIR/$SQLITE_ZIP"
            echo "    ./scripts/build-win.sh"
            echo ""
            rm -f "$SQLITE_ZIP"
            exit 1
        fi

        # 验证解压产物
        if [ ! -f "$SQLITE_DIR/sqlite3.c" ] || [ ! -f "$SQLITE_DIR/sqlite3.h" ]; then
            die "解压完成但未找到 sqlite3.c/sqlite3.h，请删除 $SQLITE_DIR 后重试"
        fi
    fi

    # 编译
    cd "$SQLITE_DIR"
    info "编译 sqlite3.c → libsqlite3.a..."

    SQLITE_LOG=$(mktemp)
    if ! $CROSS_GCC -c sqlite3.c -o sqlite3.o \
        -O2 \
        -DSQLITE_ENABLE_FTS5 \
        -DSQLITE_ENABLE_JSON1 \
        -DSQLITE_THREADSAFE=1 \
        -DSQLITE_USE_URI=1 > "$SQLITE_LOG" 2>&1; then
        error "SQLite3 编译失败:"
        grep -E "error:|warning:" "$SQLITE_LOG" 2>/dev/null || cat "$SQLITE_LOG"
        echo ""
        echo "  可能原因:"
        echo "    1. 交叉编译器异常 → $CROSS_GXX --version"
        echo "    2. 磁盘空间不足  → df -h"
        echo "    3. 源文件不完整  → rm -rf $SQLITE_DIR && ./scripts/build-win.sh"
        echo ""
        rm -f "$SQLITE_LOG"
        exit 1
    fi
    rm -f "$SQLITE_LOG"

    if ! $CROSS_AR rcs libsqlite3.a sqlite3.o 2>&1; then
        die "SQLite3 静态库打包失败"
    fi

    touch .built
    info "SQLite3 静态库构建完成 ✓"
else
    info "SQLite3 静态库已缓存 ✓"
fi

# 验证产物
if [ ! -f "$SQLITE_DIR/libsqlite3.a" ] || [ ! -f "$SQLITE_DIR/sqlite3.h" ]; then
    die "SQLite3 产物缺失，请删除缓存后重试: rm -rf $SQLITE_DIR"
fi

# ═══════════════════════════════════════════════════════════════════
# 步骤3：构建 ZLIB Windows 静态库
# ═══════════════════════════════════════════════════════════════════
section "[3/6] 构建 ZLIB for Windows"

ZLIB_VER="1.3.1"
ZLIB_DIR="$DEPS_DIR/zlib-$ZLIB_VER"
ZLIB_TGZ="zlib-$ZLIB_VER.tar.gz"

ZLIB_URLS=(
    "https://github.com/madler/zlib/releases/download/v$ZLIB_VER/zlib-$ZLIB_VER.tar.gz"
    "https://zlib.net/zlib-$ZLIB_VER.tar.gz"
    "https://zlib.net/fossils/zlib-$ZLIB_VER.tar.gz"
)

if [ ! -f "$ZLIB_DIR/.built" ]; then
    mkdir -p "$DEPS_DIR"

    # 下载
    if [ ! -f "$ZLIB_DIR/zlib.h" ]; then
        cd "$DEPS_DIR"

        DOWNLOADED=false
        if [ -f "$ZLIB_TGZ" ]; then
            info "发现已有 $ZLIB_TGZ，直接使用"
            DOWNLOADED=true
        else
            for url in "${ZLIB_URLS[@]}"; do
                info "尝试: $url"
                if curl -fSL --retry 2 --connect-timeout 10 "$url" -o "$ZLIB_TGZ" 2>/dev/null; then
                    DOWNLOADED=true
                    break
                fi
                warn "不可用，尝试下一个..."
            done
        fi

        if [ "$DOWNLOADED" = false ]; then
            error "所有 zlib 下载源均失败"
            echo ""
            echo "  手动解决办法:"
            echo "    1. 浏览器打开 https://github.com/madler/zlib/releases"
            echo "    2. 下载 zlib-$ZLIB_VER.tar.gz"
            echo "    3. 放入 $DEPS_DIR/"
            echo "    4. 重新运行此脚本"
            echo ""
            rm -f "$ZLIB_TGZ"
            exit 1
        fi

        info "解压 zlib..."
        if ! tar xzf "$ZLIB_TGZ" 2>/dev/null; then
            error "解压 $ZLIB_TGZ 失败（文件可能损坏或下载不完整）"
            echo ""
            echo "  删除后重试: rm $DEPS_DIR/$ZLIB_TGZ && ./scripts/build-win.sh"
            echo ""
            rm -f "$ZLIB_TGZ"
            exit 1
        fi

        # 验证解压产物
        if [ ! -f "$ZLIB_DIR/zlib.h" ]; then
            die "解压完成但未找到 zlib.h，请删除 $ZLIB_DIR 后重试"
        fi
    fi

    # 编译
    cd "$ZLIB_DIR"
    info "编译 zlib → libz.a..."

    ZLIB_LOG=$(mktemp)
    if ! make -f win32/Makefile.gcc -j"$(nproc)" \
        PREFIX="$CROSS_PREFIX-" \
        CFLAGS="-O2" \
        libz.a > "$ZLIB_LOG" 2>&1; then
        error "zlib 编译失败:"
        echo ""
        grep -E "error:|Error" "$ZLIB_LOG" 2>/dev/null | head -20 || cat "$ZLIB_LOG"
        echo ""
        rm -f "$ZLIB_LOG"
        echo ""
        echo "  尝试手动编译:"
        echo "    cd $ZLIB_DIR"
        echo "    $CROSS_GCC -c -O2 *.c"
        echo "    $CROSS_AR rcs libz.a *.o"
        echo "    cd $PROJECT_DIR && ./scripts/build-win.sh"
        echo ""
        exit 1
    fi
    rm -f "$ZLIB_LOG"

    touch .built
    info "ZLIB 静态库构建完成 ✓"
else
    info "ZLIB 静态库已缓存 ✓"
fi

# 验证产物
if [ ! -f "$ZLIB_DIR/libz.a" ] || [ ! -f "$ZLIB_DIR/zlib.h" ]; then
    die "ZLIB 产物缺失，请删除缓存后重试: rm -rf $ZLIB_DIR"
fi

# ═══════════════════════════════════════════════════════════════════
# 步骤4：CMake 交叉编译配置
# ═══════════════════════════════════════════════════════════════════
section "[4/6] CMake 交叉编译配置"

SQLITE3_INC="$SQLITE_DIR"
SQLITE3_LIB="$SQLITE_DIR/libsqlite3.a"
ZLIB_INC="$ZLIB_DIR"
ZLIB_LIB="$ZLIB_DIR/libz.a"

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

info "运行 CMake..."

CMAKE_LOG=$(mktemp)
if ! cmake "$SERVER_DIR" \
    -DCMAKE_TOOLCHAIN_FILE="$PROJECT_DIR/scripts/toolchain-win.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$SQLITE3_INC;$ZLIB_INC" \
    -DSQLite3_INCLUDE_DIR="$SQLITE3_INC" \
    -DSQLite3_LIBRARY="$SQLITE3_LIB" \
    -DZLIB_INCLUDE_DIR="$ZLIB_INC" \
    -DZLIB_LIBRARY="$ZLIB_LIB" \
    -DCMAKE_CXX_FLAGS="-O2 -static-libgcc -static-libstdc++" \
    -DCMAKE_EXE_LINKER_FLAGS="-static -lpthread -lws2_32" \
    > "$CMAKE_LOG" 2>&1; then

    error "CMake 配置失败！详细日志:"
    echo ""
    cat "$CMAKE_LOG"
    echo ""

    # 诊断
    if grep -q "SQLite3" "$CMAKE_LOG" 2>/dev/null; then
        echo "  ▶ SQLite3 未找到 → ls -la $SQLITE3_LIB && ls -la $SQLITE3_INC/sqlite3.h"
    fi
    if grep -q "ZLIB" "$CMAKE_LOG" 2>/dev/null; then
        echo "  ▶ ZLIB 未找到   → ls -la $ZLIB_LIB && ls -la $ZLIB_INC/zlib.h"
    fi
    if grep -qi "thread" "$CMAKE_LOG" 2>/dev/null; then
        echo "  ▶ Threads 未找到 → dpkg -l | grep mingw"
    fi

    rm -f "$CMAKE_LOG"
    exit 1
fi

rm -f "$CMAKE_LOG"
info "CMake 配置完成 ✓"

# ═══════════════════════════════════════════════════════════════════
# 步骤5：编译
# ═══════════════════════════════════════════════════════════════════
section "[5/6] 编译 bookkeeping.exe"

BUILD_LOG=$(mktemp)
if ! cmake --build . -j"$(nproc)" > "$BUILD_LOG" 2>&1; then
    error "编译失败！"
    echo ""

    # 提取所有错误行
    if grep -E "error:|undefined reference" "$BUILD_LOG" 2>/dev/null | head -30; then
        :
    else
        # 显示最后 30 行
        tail -30 "$BUILD_LOG"
    fi

    echo ""
    echo "  完整日志: $BUILD_LOG"
    echo ""
    echo "  ┌─────────────────────────────────────────────────┐"
    echo "  │ 常见编译错误及解决办法:                          │"
    echo "  │                                                 │"
    echo "  │ 'sqlite3.h' file not found                     │"
    echo "  │   → ls $SQLITE3_INC/sqlite3.h                   │"
    echo "  │                                                 │"
    echo "  │ 'zlib.h' file not found                        │"
    echo "  │   → ls $ZLIB_INC/zlib.h                        │"
    echo "  │                                                 │"
    echo "  │ undefined reference to '__imp_WSACleanup'       │"
    echo "  │   → 缺少 -lws2_32，检查 CMakeLists.txt          │"
    echo "  │                                                 │"
    echo "  │ undefined reference to 'pthread_*'              │"
    echo "  │   → sudo apt install g++-mingw-w64-x86-64-posix │"
    echo "  │                                                 │"
    echo "  │ undefined reference to 'sqlite3_*'              │"
    echo "  │   → file $SQLITE3_LIB (应为 PE 格式的 .a)       │"
    echo "  └─────────────────────────────────────────────────┘"
    echo ""
    exit 1
fi

rm -f "$BUILD_LOG"

EXE_PATH="$BUILD_DIR/bookkeeping.exe"
if [ ! -f "$EXE_PATH" ]; then
    die "编译完成但未生成 bookkeeping.exe

  检查 CMake 输出: cmake --build $BUILD_DIR --verbose"
fi

EXE_SIZE=$(du -h "$EXE_PATH" | cut -f1)
info "编译产物: bookkeeping.exe ($EXE_SIZE)"
echo ""

# 文件类型检查
info "文件类型检查:"
file "$EXE_PATH"
echo ""

# DLL 依赖检查（仅在 objdump 可用时）
if [ "$HAS_OBJDUMP" = true ]; then
    DLL_COUNT=$("$CROSS_OBJDUMP" -p "$EXE_PATH" 2>/dev/null | grep -ci "DLL Name" || echo "0")
    if [ "$DLL_COUNT" -eq 0 ]; then
        info "链接方式: 纯静态链接 ✓ (无需额外 DLL)"
    else
        warn "检测到 $DLL_COUNT 个 DLL 依赖（非纯静态链接）:"
        "$CROSS_OBJDUMP" -p "$EXE_PATH" 2>/dev/null | grep -i "DLL Name" || true
        echo ""
        echo "  如果这些是系统 DLL (kernel32.dll 等)，通常不影响使用。"
        echo "  如果包含 libgcc_*.dll 或 libstdc++*.dll，说明静态链接未生效。"
    fi
else
    warn "跳过 DLL 检查（$CROSS_OBJDUMP 未安装）"
fi
echo ""

# ═══════════════════════════════════════════════════════════════════
# 步骤6：组装发布目录
# ═══════════════════════════════════════════════════════════════════
section "[6/6] 组装发布目录"

# 前置检查: frontend 目录
if [ ! -d "$SERVER_DIR/frontend" ]; then
    die "未找到 $SERVER_DIR/frontend/ 目录，无法打包前端文件"
fi

rm -rf "$DIST_DIR"
mkdir -p "$DIST_DIR"

# 复制 exe
cp "$EXE_PATH" "$DIST_DIR/bookkeeping-server.exe" || die "复制 exe 失败"
info "已复制: bookkeeping-server.exe"

# 复制 categories.json
cp "$SERVER_DIR/categories.json" "$DIST_DIR/" || die "复制 categories.json 失败"
info "已复制: categories.json"

# 复制前端文件
if command -v rsync &>/dev/null; then
    rsync -a --exclude='.git' --exclude='*.swp' --exclude='*~' \
        "$SERVER_DIR/frontend/" "$DIST_DIR/frontend/"
else
    cp -r "$SERVER_DIR/frontend" "$DIST_DIR/frontend"
fi
info "已复制: frontend/"

# 复制 bat 启动器，并转换为 CRLF 换行
BAT_SRC="$PROJECT_DIR/scripts/run.bat"
BAT_DST="$DIST_DIR/run.bat"
if [ -f "$BAT_SRC" ]; then
    # 转换 LF → CRLF（Windows .bat 文件必须 CRLF）
    sed 's/$/\r/' "$BAT_SRC" > "$BAT_DST"
    info "已复制: run.bat (已转换 CRLF)"
else
    warn "未找到 $BAT_SRC，跳过"
fi

echo ""
info "发布目录内容:"
find "$DIST_DIR" -type f | sed "s|$DIST_DIR/|  • |" | sort
echo ""

# ── 最终摘要 ──────────────────────────────────────────────────────
echo "══════════════════════════════════════════════════════════════"
echo "  ✅ 交叉编译成功！"
echo ""
echo "  EXE:  $DIST_DIR/bookkeeping-server.exe ($EXE_SIZE)"
echo ""
echo "  Windows 使用:"
echo "    1. 复制 dist/Bookkeeping/ 到 Windows"
echo "    2. 双击 run.bat"
echo "    3. 浏览器打开 http://127.0.0.1:18080"
echo ""
echo "  💡 提示:"
echo "    • run.bat 已自动转换为 CRLF 换行，兼容 cmd.exe"
echo "    • 如果 Windows 报缺失 DLL，检查上方 DLL 依赖输出"
echo "    • 如果前端无法显示，确认 frontend/ 目录存在且含 index.html"
echo "══════════════════════════════════════════════════════════════"
