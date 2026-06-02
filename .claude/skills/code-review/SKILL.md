---
name: code-review
description: Review C++ / JS / Shell / CMake changes in the bookkeeping project for bugs, security, and style violations. Trigger when user asks for code review, pre-commit check, or "check my changes".
---

## 审查清单

按 CLAUDE.md 编码规范逐项检查当前变更：

### C++ (`server/src/*.cpp`, `server/src/*.h`)

- [ ] 头文件用 `#pragma once`
- [ ] include 顺序正确：自身 .h → 项目内 .h → 第三方 → 标准库
- [ ] 禁止 `using namespace std;`（允许局部 `namespace fs = std::filesystem;`）
- [ ] 资源用 RAII：智能指针 / 栈对象 / STL 容器，禁止裸 `new`/`delete`
- [ ] 纯虚类/工厂考虑用 `std::unique_ptr` 管理生命周期
- [ ] 私有成员变量用 `snake_case_` 后缀
- [ ] 公开 API 有 Doxygen `/** */` 注释
- [ ] `std::stoi` / `std::stod` 必须 try-catch
- [ ] SQL 拼接必须走 `Database::escape()` 或参数化
- [ ] 致命错误输出到 `std::cerr` + 返回非零值
- [ ] 新增 .cpp 已加入 `server/CMakeLists.txt` 的 `SOURCES` 列表

### JavaScript (`server/frontend/app.js`)

- [ ] 用 `const` / `let`，不用 `var`
- [ ] fetch 响应检查 `res.ok`，失败抛 Error
- [ ] DOM 操作和 API 调用分离（`API.xxx` / `UI.xxx`）

### Shell (`scripts/*.sh`)

- [ ] 脚本开头有 `set -euo pipefail`
- [ ] 每一步操作检查返回值，失败给出原因和解决办法
- [ ] 路径用变量，禁止硬编码 `/home/xxx`
- [ ] 用 `[[ ]]` 做条件判断
- [ ] 输出用 `info()` / `warn()` / `error()` 颜色函数

### CMake (`server/CMakeLists.txt`)

- [ ] 用 `target_*` 命令
- [ ] 新增源文件已加入 `SOURCES` 列表
- [ ] 平台条件用 `if(WIN32)`

### 通用

- [ ] 无硬编码绝对路径
- [ ] 无密码/Token/密钥

---

## 输出格式

按严重程度排序：

| 级别 | 含义 | 示例 |
|------|------|------|
| 🔴 阻断 | 安全漏洞 / 会 crash | SQL 注入、未捕获异常、空指针解引用 |
| 🟡 警告 | 违反编码规范 | 命名不规范、缺少注释、未用 RAII |
| 🔵 建议 | 可读性 / 性能优化 | 代码简化、去重 |

每个问题附：**文件:行号** + **违规内容** + **修复建议**。
