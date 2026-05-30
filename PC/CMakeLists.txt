cmake_minimum_required(VERSION 3.16)
project(bookkeeping VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── 依赖库 ────────────────────────────────────────────────────
find_package(SQLite3 REQUIRED)
find_package(Threads REQUIRED)

# ── 源文件 ────────────────────────────────────────────────────
set(SOURCES
    src/main.cpp
    src/db.cpp
    src/handlers.cpp
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
add_custom_command(TARGET bookkeeping POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/frontend
        $<TARGET_FILE_DIR:bookkeeping>/frontend
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/categories.json
        $<TARGET_FILE_DIR:bookkeeping>/categories.json
    COMMENT "正在复制前端文件和分类模板到构建目录"
)
