# 个人记账 (Bookkeeping) — 项目开发规范

## 项目概述

C++17 记账 HTTP 服务 + Web 前端，SQLite 本地存储，完全离线。
支持 PC 桌面端、WSL、Windows(.exe 交叉编译)。

## 目录结构

```
server/src/       C++ 源码（一个 .h 配一个 .cpp）
server/vendor/    第三方头文件（httplib.h, json.hpp）
server/frontend/  Web 前端（index.html / style.css / app.js）
scripts/          构建与启动脚本
```

---

## C++ 规范

**文件组织**
- 一个 `.h` 配一个 `.cpp`，文件名 `snake_case`
- 头文件用 `#pragma once`
- include 顺序：自身 .h → 项目内 .h → 第三方库 → 标准库，组间空行

**命名**
| 类型 | 风格 | 示例 |
|------|------|------|
| 类/结构体 | PascalCase | `Database`, `RecordQuery` |
| 函数 | camelCase | `queryRecords()`, `getCategories()` |
| 成员变量 | snake_case_ | `db_`, `page_size_` |
| 局部变量/参数 | snake_case | `db_path`, `date_from` |
| 常量 | UPPER_SNAKE_CASE | `MAX_RETRIES` |

**错误处理**
- 数据库：返回 `bool` / `std::optional<T>`，不抛异常
- `std::stoi` / `std::stod` 必须 try-catch
- 致命错误：`std::cerr` + `return 1`

**禁止**
- ❌ `using namespace std;`
- ❌ `new`/`delete`（用 RAII、STL 容器、智能指针）
- ❌ 硬编码绝对路径
- ❌ SQL 拼接不做转义（必须走 `Database::escape()` 或参数化）

---

## JavaScript 规范

- 用 `const` / `let`，不用 `var`
- 常量 `UPPER_SNAKE`，变量/函数 `camelCase`
- API 调用封装到 `API` 对象，DOM 操作封装到 `UI` 对象
- fetch 必须检查 `res.ok`

---

## Shell 脚本规范

- **必须** `set -euo pipefail`
- 用颜色输出函数：`info()` `warn()` `error()`
- 环境工具检查：每一步操作检查返回值，失败给原因+解决办法
- 编译错误捕获到临时文件，失败时 grep 错误行展示
- 路径用变量，禁止硬编码

---

## 注释规范

> 详细审查清单见 `.claude/skills/comment-review/SKILL.md`

### 核心原则（三条）

1. **写"为什么"而非"是什么"** — 代码已经说了"是什么"，注释要回答"为什么选这个值 / 这个方案 / 这个顺序"
2. **一眼定位** — 打开任意文件 5 秒内能扫到：这文件干嘛的、分哪几块、关键决策在哪
3. **不腐烂** — 改代码同步改注释，注释掉的代码直接删掉（git 里有历史）

### 格式速查

| 场景 | 格式 | 示例 |
|------|------|------|
| 文件头 | `// 文件名 — 一句话职责` | `// db.h — SQLite 持久化层接口` |
| C++ 公开方法 | `/** 一句话说明 */` | `/** 查询记录列表，支持分页筛选排序 */` |
| 行内"为什么" | `// 原因` | `int port = 18080;  // 与 run.sh / README 保持一致` |
| 段落分隔（C++） | `// ── 段落名（中文）──` | `// ── 路径解析 ──` |
| 段落分隔（Shell） | `# ── 步骤 N：说明 ──` | `# ── 步骤1：CMake 配置 ──` |
| 段落分隔（JS） | `// ── 段落名 ──` | `// ── API 封装 ──` |

### 新增代码的底线

以下任一缺失视为**未完成**，不应提交：

- [ ] 新文件有文件头（`// xxx.xxx — 职责说明`）
- [ ] 新函数/方法有注释（用途 + 关键参数，一行够用不求 Doxygen 长文）
- [ ] 新类/结构体有注释（代表什么、谁在用）
- [ ] 关键设计决策有"为什么"注释
- [ ] 长文件有 `// ── xxx ──` 段落分隔

### 禁止

- ❌ 注释掉的代码（用 `git log` 回溯）
- ❌ 重复代码的废话注释（`i++; // i 自增`）
- ❌ `/* */` 在函数体内（`//` 统一风格）

### 文档自动维护规则

**项目中的所有 `.md` 文件必须保持一致，任何代码变更都须同步更新关联文档。**

#### 受维护的文档清单

| 文件 | 职责 |
|------|------|
| `README.md` | 项目概览、结构图、环境要求、API 表 |
| `requirements.md` | 功能需求清单 |
| `.claude/CLAUDE.md` | 编码规范 |
| `.claude/skills/*/SKILL.md` | 各 skill 的审查/流程规则 |

#### 具体同步规则

| 变更类型 | 必须同步的文档 |
|---------|--------------|
| 新增/删除/重命名目录或文件（含脚本） | `README.md`（项目结构图） |
| 新增/修改 C++ 源文件 | `server/CMakeLists.txt` + `README.md`（src 树） |
| 新增/修改/删除 API 端点 | `README.md`（API 表格）+ `requirements.md`（功能清单） |
| 修改编码规范 | `.claude/skills/code-review/SKILL.md` |
| 新增/删除 skill | `README.md`（.claude 节）+ `.claude/settings.json` |
| 修改数据库 schema | `README.md` + `.claude/skills/db-safe/SKILL.md` |
| 修改构建依赖/环境要求 | `README.md`（环境要求）+ `requirements.md` |
| 修改端口号 | `README.md` + `scripts/run.sh` + `scripts/run.bat` |

**此规则无例外。** 修改代码时，第一步确认哪些文档需要更新，第二步改代码同时改文档，第三步验证文档间无矛盾。


- 提交信息：`【类型】描述`（类型：新增/修改/修复/优化/重构/文档）
- 一个提交只做一件事
- 不提交构建产物（`build/` `build-mingw/` `dist/`）
