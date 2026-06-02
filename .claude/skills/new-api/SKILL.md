---
name: new-api
description: Guide for adding a new REST API endpoint to the bookkeeping server. Trigger when user wants to add a new route, create a new REST endpoint, or extend the HTTP API.
---

## 新增 API 端点完整流程

### 后端（C++）

#### 1. 声明数据库接口（`server/src/db.h`）

如果需要新的数据库操作，在 `Database` 类中声明：

```cpp
// 示例
bool deleteRecordsByCategory(int category_l1_id);  // 按分类删除记录
```

加上 Doxygen 注释说明入参、返回值、副作用。

#### 2. 实现数据库接口（`server/src/db.cpp`）

- 用 `sqlite3_prepare_v2` + `sqlite3_bind_*` + `sqlite3_step` 模式
- 写操作检查返回值：成功返回 `true` / `SQLITE_DONE == rc`
- 查询操作：解析结果集 → 填充结构体 → 返回
- 字符串拼接 SQL 必须走 `Database::escape()`

#### 3. 注册路由（`server/src/handlers.cpp`）

在 `registerRoutes()` 函数中注册：

```cpp
// GET /api/xxx — 查询
svr.Get("/api/xxx", [&](const httplib::Request& req, httplib::Response& res) {
    try {
        // 1. 从 req 解析查询参数
        // 2. 调用 db.xxx()
        // 3. 构造 JSON 响应
        json j;
        j["ok"] = true;
        j["data"] = result;
        res.set_content(j.dump(), "application/json");
    } catch (const std::exception& e) {
        json j;
        j["ok"] = false;
        j["error"] = e.what();
        res.set_content(j.dump(), "application/json");
        res.status = 400;
    }
});
```

#### 4. 注册到 CMake

如果新增了 `.cpp` 文件，加入 `server/CMakeLists.txt` 的 `SOURCES` 列表。

### 前端（JavaScript）

在 `server/frontend/app.js` 中：

1. 如果有新的 API 调用模式，封装到 `API` 对象
2. 界面更新逻辑封装到 `UI` 对象
3. 错误处理：在 catch 块显示提示信息

### 验证

```bash
# 1. 构建
./scripts/run.sh

# 2. 用 curl 测试新接口
curl http://127.0.0.1:18080/api/xxx

# 3. 浏览器测试前端交互
```
