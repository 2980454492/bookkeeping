# CMake 工具链文件：x86_64-w64-mingw32 交叉编译
# 用法: cmake -DCMAKE_TOOLCHAIN_FILE=PC/cmake/toolchain-mingw64.cmake ...

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# 交叉编译器前缀
set(TOOLCHAIN_PREFIX x86_64-w64-mingw32)

set(CMAKE_C_COMPILER   ${TOOLCHAIN_PREFIX}-gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}-g++)
set(CMAKE_RC_COMPILER  ${TOOLCHAIN_PREFIX}-windres)
set(CMAKE_AR           ${TOOLCHAIN_PREFIX}-ar)
set(CMAKE_RANLIB       ${TOOLCHAIN_PREFIX}-ranlib)

# 告诉 CMake 这是交叉编译
set(CMAKE_CROSSCOMPILING TRUE)

# BOTH 模式：允许 CMake 同时在 sysroot 和宿主路径中搜索
# 这样手工编译的依赖库可以被 find_package/find_library 找到
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
