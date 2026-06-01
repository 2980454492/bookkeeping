cmake_minimum_required(VERSION 3.16)
project(bookkeeping VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── 依赖库 ────────────────────────────────────────────────────
find_package(SQLite3 REQUIRED)
find_package(Threads REQUIRED)

# ── 源文件 ────────────────────────────────────────────────────
set(SOURCES
    src/main.cpp
    src/db.cpp
    src/handlers.cpp
    src/export_util.cpp
    src/import_util.cpp
)

# ── 可执行文件 ──────────────────────────────────────────────────
add_executable(bookkeeping ${SOURCES})

target_include_directories(bookkeeping PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/libs    # httplib.h + json.hpp
)

target_link_libraries(bookkeeping PRIVATE
    SQLite::SQLite3
    Threads::Threads
)

# ── 将资源文件复制到构建目录 ───────────────────────────────────
# 用「始终执行」的自定义目标而非 POST_BUILD：POST_BUILD 只在可执行文件重新链接时才运行，
# 仅修改 frontend/ 等资源（C++ 源码未变）时不会触发，导致 build/ 下前端文件停留在旧版本。
# 该 target 无输出文件，CMake 视其永远过期，每次 `cmake --build` 都会重新复制。
add_custom_target(copy_assets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/frontend
        $<TARGET_FILE_DIR:bookkeeping>/frontend
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/categories.json
        $<TARGET_FILE_DIR:bookkeeping>/categories.json
    COMMENT "正在复制前端文件和分类模板到构建目录"
)
add_dependencies(copy_assets bookkeeping)
