#!/bin/bash
# ═══════════════════════════════════════════════════════════════════
# WSL → Windows 交叉编译脚本
# 在 WSL/Linux 中生成 bookkeeping-server.exe（无需 Visual Studio）
#
# 用法:
#   ./scripts/build-win.sh          # 正常编译
#   ./scripts/build-win.sh clean    # 清除缓存后编译
# ═══════════════════════════════════════════════════════════════════
set -euo pipefail

# ── 颜色输出 ──────────────────────────────────────────────────────
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BOLD='\033[1m'
NC='\033[0m' # No Color

info()    { echo -e "${GREEN}==>${NC} $*"; }
warn()    { echo -e "${YELLOW}⚠️  $*${NC}"; }
error()   { echo -e "${RED}❌ $*${NC}"; }
section() { echo ""; echo -e "${BOLD}── $* ──${NC}"; echo ""; }

# ── 路径常量 ──────────────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
SERVER_DIR="$PROJECT_DIR/server"
BUILD_DIR="$PROJECT_DIR/build-mingw"
DEPS_DIR="$BUILD_DIR/_deps"
DIST_DIR="$PROJECT_DIR/dist/Bookkeeping"
CROSS_PREFIX="x86_64-w64-mingw32"

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

MISSING=()

# 1a. 交叉编译器
CROSS_GCC="${CROSS_PREFIX}-gcc"
CROSS_GXX="${CROSS_PREFIX}-g++"
CROSS_AR="${CROSS_PREFIX}-ar"

if ! command -v "$CROSS_GXX" &>/dev/null; then
    error "未找到 mingw-w64 交叉编译器 ($CROSS_GXX)"
    echo ""
    echo "  请运行以下命令安装："
    echo ""
    echo "    sudo apt update"
    echo "    sudo apt install -y g++-mingw-w64-x86-64-posix"
    echo ""
    echo "  安装完成后重新运行此脚本。"
    exit 1
fi
info "交叉编译器: $($CROSS_GXX --version | head -1)"

# 1b. CMake
if ! command -v cmake &>/dev/null; then
    MISSING+=("cmake")
else
    info "CMake:      $(cmake --version | head -1)"
fi

# 1c. Python3 (用于解压 zip)
if ! command -v python3 &>/dev/null; then
    MISSING+=("python3")
else
    info "Python3:    $(python3 --version)"
fi

# 1d. curl
if ! command -v curl &>/dev/null; then
    MISSING+=("curl")
fi

# 1e. rsync (用于复制前端文件)
if ! command -v rsync &>/dev/null; then
    MISSING+=("rsync")
fi

if [ ${#MISSING[@]} -gt 0 ]; then
    echo ""
    error "缺少以下工具: ${MISSING[*]}"
    echo ""
    echo "  请运行以下命令安装："
    echo ""
    echo "    sudo apt install -y ${MISSING[*]}"
    echo ""
    exit 1
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
    if [ ! -f "$f" ]; then
        MISSING_FILES+=("$f")
    fi
done
if [ ${#MISSING_FILES[@]} -gt 0 ]; then
    error "缺少项目源文件:"
    for f in "${MISSING_FILES[@]}"; do
        echo "     - $f"
    done
    echo ""
    echo "  请确认项目目录完整。httplib.h 和 json.hpp 需要手动下载："
    echo ""
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

# 如果下载失败可尝试替换版本号
SQLITE_VER="3450300"
SQLITE_YEAR="2024"
SQLITE_DIR="$DEPS_DIR/sqlite-amalgamation-$SQLITE_VER"
SQLITE_ZIP="sqlite-amalgamation-$SQLITE_VER.zip"
SQLITE_URL="https://www.sqlite.org/$SQLITE_YEAR/$SQLITE_ZIP"

# 备用下载源（版本可能不同，但 amalgamation 接口稳定）
SQLITE_URL_BACKUP="https://github.com/sqlite/sqlite/raw/version-3.45.0/sqlite3.c"

if [ ! -f "$SQLITE_DIR/.built" ]; then
    mkdir -p "$DEPS_DIR"

    # 下载
    if [ ! -f "$SQLITE_DIR/sqlite3.c" ]; then
        cd "$DEPS_DIR"

        if [ ! -f "$SQLITE_ZIP" ]; then
            info "下载 SQLite amalgamation..."
            if ! curl -fSL --retry 3 --connect-timeout 10 \
                "$SQLITE_URL" -o "$SQLITE_ZIP"; then
                warn "主下载源失败: $SQLITE_URL"
                warn "请检查网络连接或手动下载:"
                echo ""
                echo "    curl -L '$SQLITE_URL' -o $SQLITE_ZIP"
                echo ""
                echo "  然后重新运行此脚本。"
                echo ""
                echo "  如果官网持续不可用，可尝试:"
                echo "    1. 浏览器打开 https://www.sqlite.org/download.html"
                echo "    2. 搜索 'sqlite-amalgamation' 下载最新 zip"
                echo "    3. 放入 $DEPS_DIR/"
                echo ""
                rm -f "$SQLITE_ZIP"
                exit 1
            fi
            info "下载完成 ($(du -h "$SQLITE_ZIP" | cut -f1))"
        fi

        info "解压..."
        if ! python3 -c "
import zipfile, sys
try:
    with zipfile.ZipFile('$SQLITE_ZIP') as zf:
        zf.extractall()
    print('解压成功')
except Exception as e:
    print(f'解压失败: {e}', file=sys.stderr)
    sys.exit(1)
"; then
            error "解压 $SQLITE_ZIP 失败，文件可能损坏"
            echo ""
            echo "  解决办法: 删除损坏文件后重试"
            echo "    rm $DEPS_DIR/$SQLITE_ZIP"
            echo "    ./scripts/build-win.sh"
            echo ""
            rm -f "$SQLITE_ZIP"
            exit 1
        fi
    fi

    # 编译
    cd "$SQLITE_DIR"
    info "编译 sqlite3.c → libsqlite3.a..."

    if ! $CROSS_GCC -c sqlite3.c -o sqlite3.o \
        -O2 \
        -DSQLITE_ENABLE_FTS5 \
        -DSQLITE_ENABLE_JSON1 \
        -DSQLITE_THREADSAFE=1 \
        -DSQLITE_USE_URI=1 2>&1; then
        error "SQLite3 编译失败"
        echo ""
        echo "  可能原因:"
        echo "    1. 交叉编译器异常 → 运行 ${CROSS_GXX} --version 检查"
        echo "    2. 磁盘空间不足 → 运行 df -h 检查"
        echo "    3. amalgamation 文件不完整 → 删除 $SQLITE_DIR 后重试"
        echo ""
        exit 1
    fi

    if ! $CROSS_AR rcs libsqlite3.a sqlite3.o; then
        error "SQLite3 静态库打包失败"
        exit 1
    fi

    touch .built
    info "SQLite3 静态库构建完成 ✓"
else
    info "SQLite3 静态库已缓存 ✓"
fi

# 验证产物
if [ ! -f "$SQLITE_DIR/libsqlite3.a" ] || [ ! -f "$SQLITE_DIR/sqlite3.h" ]; then
    error "SQLite3 产物缺失，请删除缓存后重试: rm -rf $SQLITE_DIR"
    exit 1
fi

# ═══════════════════════════════════════════════════════════════════
# 步骤3：构建 ZLIB Windows 静态库
# ═══════════════════════════════════════════════════════════════════
section "[3/6] 构建 ZLIB for Windows"

ZLIB_VER="1.3.1"
ZLIB_DIR="$DEPS_DIR/zlib-$ZLIB_VER"
ZLIB_TGZ="zlib-$ZLIB_VER.tar.gz"

# 多个下载源，自动尝试
ZLIB_URLS=(
    "https://github.com/madler/zlib/releases/download/v$ZLIB_VER/zlib-$ZLIB_VER.tar.gz"
    "https://zlib.net/zlib-$ZLIB_VER.tar.gz"
    "https://zlib.net/fossils/zlib-$ZLIB_VER.tar.gz"
    "https://managedway.dl.sourceforge.net/project/libpng/zlib/$ZLIB_VER/zlib-$ZLIB_VER.tar.gz"
)

if [ ! -f "$ZLIB_DIR/.built" ]; then
    mkdir -p "$DEPS_DIR"

    # 下载（尝试多个源）
    if [ ! -f "$ZLIB_DIR/zlib.h" ]; then
        cd "$DEPS_DIR"

        DOWNLOADED=false
        if [ -f "$ZLIB_TGZ" ]; then
            info "发现已有 $ZLIB_TGZ，直接使用"
            DOWNLOADED=true
        else
            for url in "${ZLIB_URLS[@]}"; do
                info "尝试下载 zlib: $url"
                if curl -fSL --retry 2 --connect-timeout 10 "$url" -o "$ZLIB_TGZ" 2>/dev/null; then
                    DOWNLOADED=true
                    break
                fi
                warn "该源不可用，尝试下一个..."
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
            error "解压 $ZLIB_TGZ 失败，文件可能损坏"
            echo ""
            echo "  解决办法: 删除损坏文件后重试"
            echo "    rm $DEPS_DIR/$ZLIB_TGZ"
            echo "    ./scripts/build-win.sh"
            echo ""
            rm -f "$ZLIB_TGZ"
            exit 1
        fi
    fi

    # 交叉编译 zlib
    cd "$ZLIB_DIR"
    info "编译 zlib → libz.a..."

    if ! make -f win32/Makefile.gcc -j"$(nproc)" \
        PREFIX="$CROSS_PREFIX-" \
        CFLAGS="-O2" \
        libz.a 2>&1 | tail -10; then
        error "zlib 编译失败"
        echo ""
        echo "  可能原因:"
        echo "    1. Makefile.win32 与交叉编译器不兼容"
        echo "    2. 磁盘空间不足"
        echo ""
        echo "  尝试手动编译:"
        echo "    cd $ZLIB_DIR"
        echo "    ${CROSS_PREFIX}-gcc -c -O2 *.c"
        echo "    ${CROSS_PREFIX}-ar rcs libz.a *.o"
        echo ""
        exit 1
    fi

    touch .built
    info "ZLIB 静态库构建完成 ✓"
else
    info "ZLIB 静态库已缓存 ✓"
fi

# 验证产物
if [ ! -f "$ZLIB_DIR/libz.a" ] || [ ! -f "$ZLIB_DIR/zlib.h" ]; then
    error "ZLIB 产物缺失，请删除缓存后重试: rm -rf $ZLIB_DIR"
    exit 1
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

# 捕获 CMake 输出用于诊断
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

    error "CMake 配置失败！以下是详细日志:"
    echo ""
    cat "$CMAKE_LOG"
    echo ""

    # 诊断常见问题
    if grep -q "SQLite3" "$CMAKE_LOG" 2>/dev/null; then
        echo "  ┌─────────────────────────────────────────────┐"
        echo "  │ SQLite3 未找到                              │"
        echo "  │ 检查: ls -la $SQLITE3_LIB                   │"
        echo "  │       ls -la $SQLITE3_INC/sqlite3.h          │"
        echo "  └─────────────────────────────────────────────┘"
    fi
    if grep -q "ZLIB" "$CMAKE_LOG" 2>/dev/null; then
        echo "  ┌─────────────────────────────────────────────┐"
        echo "  │ ZLIB 未找到                                 │"
        echo "  │ 检查: ls -la $ZLIB_LIB                      │"
        echo "  │       ls -la $ZLIB_INC/zlib.h               │"
        echo "  └─────────────────────────────────────────────┘"
    fi
    if grep -qi "thread" "$CMAKE_LOG" 2>/dev/null; then
        echo "  ┌─────────────────────────────────────────────┐"
        echo "  │ Threads 未找到                              │"
        echo "  │ pthread 应包含在 mingw-w64 中               │"
        echo "  │ 检查: dpkg -l | grep mingw                  │"
        echo "  └─────────────────────────────────────────────┘"
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
    error "编译失败！以下是错误详情:"
    echo ""

    # 只显示有错误/警告的行
    grep -E "error:|warning:|Error|undefined" "$BUILD_LOG" 2>/dev/null || cat "$BUILD_LOG"
    echo ""
    echo "  完整日志: $BUILD_LOG"
    echo ""
    echo "  ┌─────────────────────────────────────────────┐"
    echo "  │ 常见编译错误及解决办法:                      │"
    echo "  │                                             │"
    echo "  │ • 'sqlite3.h' file not found               │"
    echo "  │   → SQLite3 头文件路径不正确                 │"
    echo "  │   → 检查: ls $SQLITE3_INC/sqlite3.h         │"
    echo "  │                                             │"
    echo "  │ • 'zlib.h' file not found                  │"
    echo "  │   → ZLIB 头文件路径不正确                    │"
    echo "  │   → 检查: ls $ZLIB_INC/zlib.h              │"
    echo "  │                                             │"
    echo "  │ • undefined reference to 'sqlite3_*'        │"
    echo "  │   → SQLite3 库链接失败                       │"
    echo "  │   → 检查: file $SQLITE3_LIB                 │"
    echo "  │                                             │"
    echo "  │ • undefined reference to 'deflate*'         │"
    echo "  │   → ZLIB 库链接失败                          │"
    echo "  │   → 检查: file $ZLIB_LIB                    │"
    echo "  │                                             │"
    echo "  │ • undefined reference to 'WSASocket*'       │"
    echo "  │   → 缺少 -lws2_32，检查链接参数              │"
    echo "  │                                             │"
    echo "  │ • undefined reference to 'pthread_*'        │"
    echo "  │   → 缺少 -lpthread，或 pthread 未安装       │"
    echo "  │   → sudo apt install g++-mingw-w64-x86-64   │"
    echo "  └─────────────────────────────────────────────┘"
    echo ""
    exit 1
fi

rm -f "$BUILD_LOG"

EXE_PATH="$BUILD_DIR/bookkeeping.exe"
if [ ! -f "$EXE_PATH" ]; then
    error "编译完成但未生成 bookkeeping.exe"
    echo ""
    echo "  这是不正常的。请检查 CMake 配置是否正确。"
    echo "  尝试: cmake --build $BUILD_DIR --verbose"
    echo ""
    exit 1
fi

EXE_SIZE=$(du -h "$EXE_PATH" | cut -f1)
info "编译产物: $EXE_PATH ($EXE_SIZE)"
echo ""

# 检查是否纯静态链接
info "文件类型检查:"
file "$EXE_PATH"
echo ""

DLL_COUNT=$("${CROSS_PREFIX}-objdump" -p "$EXE_PATH" 2>/dev/null | grep -ci "DLL Name" || echo "0")
if [ "$DLL_COUNT" -eq 0 ]; then
    info "链接方式: 纯静态链接 ✓ (无需额外 DLL)"
else
    warn "检测到 $DLL_COUNT 个 DLL 依赖（可能不是纯静态链接）"
    "${CROSS_PREFIX}-objdump" -p "$EXE_PATH" 2>/dev/null | grep -i "DLL Name" || true
fi

# ═══════════════════════════════════════════════════════════════════
# 步骤6：组装发布目录
# ═══════════════════════════════════════════════════════════════════
section "[6/6] 组装发布目录"

rm -rf "$DIST_DIR"
mkdir -p "$DIST_DIR"

cp "$EXE_PATH" "$DIST_DIR/bookkeeping-server.exe"
cp "$SERVER_DIR/categories.json" "$DIST_DIR/"

# 复制前端文件
if command -v rsync &>/dev/null; then
    rsync -a --exclude='.git' --exclude='*.swp' --exclude='*~' \
        "$SERVER_DIR/frontend/" "$DIST_DIR/frontend/"
else
    cp -r "$SERVER_DIR/frontend" "$DIST_DIR/frontend"
fi

# 复制 bat 启动器
BAT_LAUNCHER="$PROJECT_DIR/scripts/run.bat"
if [ -f "$BAT_LAUNCHER" ]; then
    cp "$BAT_LAUNCHER" "$DIST_DIR/"
fi

info "发布目录内容:"
echo ""
find "$DIST_DIR" -type f | sed "s|$DIST_DIR/|  • |" | sort
echo ""

# ── 最终摘要 ──────────────────────────────────────────────────────
echo "══════════════════════════════════════════════════════════════"
echo "  ✅ 交叉编译成功！"
echo ""
echo "  发布目录: $DIST_DIR"
echo "  EXE 文件: bookkeeping-server.exe ($EXE_SIZE)"
echo ""
echo "  在 Windows 中使用:"
echo "    1. 复制 dist/Bookkeeping/ 到 Windows 任意目录"
echo "    2. 双击 run.bat"
echo "    3. 浏览器访问 http://127.0.0.1:18080"
echo ""
echo "  💡 如果 Windows 上报缺失 DLL 错误:"
echo "    请检查编译日志中的链接方式是否为纯静态。"
echo "    如果提示缺少 libgcc_*.dll 或 libstdc++*.dll，"
echo "    说明静态链接未生效。运行以下命令诊断:"
echo ""
echo "      ${CROSS_PREFIX}-objdump -p $EXE_PATH | grep 'DLL Name'"
echo ""
echo "  💡 如果浏览器无法打开前端页面:"
echo "    确认 dist/Bookkeeping/frontend/ 目录存在且含 index.html"
echo "══════════════════════════════════════════════════════════════"
