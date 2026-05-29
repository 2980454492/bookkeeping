cmake_minimum_required(VERSION 3.16)
project(bookkeeping VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Dependencies ────────────────────────────────────────────────────
find_package(SQLite3 REQUIRED)
find_package(Threads REQUIRED)

# ── Source files ────────────────────────────────────────────────────
set(SOURCES
    src/main.cpp
    src/db.cpp
    src/handlers.cpp
)

# ── Executable ──────────────────────────────────────────────────────
add_executable(bookkeeping ${SOURCES})

target_include_directories(bookkeeping PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/libs    # httplib.h + json.hpp
)

target_link_libraries(bookkeeping PRIVATE
    SQLite::SQLite3
    Threads::Threads
)

# ── Copy resources to build directory ───────────────────────────────
add_custom_command(TARGET bookkeeping POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/frontend
        $<TARGET_FILE_DIR:bookkeeping>/frontend
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/categories.json
        $<TARGET_FILE_DIR:bookkeeping>/categories.json
    COMMENT "Copying frontend and categories.json to build dir"
)
