---
name: bookkeeping-build-check
description: Verify the bookkeeping project builds successfully on both Linux and Windows (cross-compile) before committing. Trigger when user is about to commit, wants to verify changes compile, or asks "does it build?".
---

## 构建检查流程

### 步骤 1：Linux 构建

```bash
rm -rf build/
./scripts/run.sh
```

等待服务启动后，快速验证 `/api/health` 返回 200，然后 Ctrl+C 停止。

**检查点：**
- `cmake` 配置无错误
- `cmake --build` 编译无警告
- 服务启动后能响应健康检查

**如果失败：**
1. 确认 `libsqlite3-dev` 和 `zlib1g-dev` 已安装
2. 检查新增 .cpp/.h 是否加入 `server/CMakeLists.txt`
3. 检查 include 路径是否正确（新文件可能在 `server/src/` 下找不到）

### 步骤 2：Windows 交叉编译

```bash
rm -rf build-mingw/ dist/
./scripts/build-win.sh
```

**检查点：**
- SQLite3/ZLIB 依赖库构建成功
- CMake 交叉编译配置成功
- `bookkeeping.exe` 生成成功
- DLL 检查显示纯静态链接

**如果失败：**
1. 确认 `g++-mingw-w64-x86-64-posix` 已安装
2. 检查 `server/CMakeLists.txt` 中 Windows 平台是否链了 `ws2_32`
3. 查看编译日志中的 `undefined reference` 错误行

### 步骤 3：报告

```
✅ Linux 构建: 通过 (X 秒)
✅ Windows 交叉编译: 通过 (X 秒)
   产物: dist/Bookkeeping/bookkeeping-server.exe (X MB)
   链接: 纯静态

⚠️  警告: (如有)
❌ 失败: (如有)
```
