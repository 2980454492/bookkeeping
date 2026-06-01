---
name: bookkeeping-db-safe
description: Rules for safely modifying the SQLite database schema. Trigger when user wants to add a table, add a column, change a schema, or modify database structure.
---

## 数据库变更安全规则

### 核心原则

1. **SQLite 不支持 ALTER TABLE 的部分操作**（如 DROP COLUMN 仅在 3.35+ 支持）
2. **必须向前兼容**——旧版本的数据文件在新版本代码上必须能正常打开
3. **自动迁移**——用户不需要手动执行 SQL

### 变更流程

#### 新增表

```cpp
// 在 createTables() 中添加 CREATE TABLE IF NOT EXISTS
const char* sql = R"(
    CREATE TABLE IF NOT EXISTS new_table (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        ...字段定义...
        created_at TEXT NOT NULL DEFAULT (datetime('now','localtime'))
    );
)";
executeSql(sql);
```

#### 新增字段

SQLite 不支持 `ALTER TABLE ADD COLUMN IF NOT EXISTS`，需要手动检查：

```cpp
// 检查列是否存在，不存在则添加
sqlite3_stmt* stmt;
int rc = sqlite3_prepare_v2(db_, "SELECT new_column FROM records LIMIT 0", -1, &stmt, nullptr);
if (rc != SQLITE_OK) {
    // 列不存在，添加
    executeSql("ALTER TABLE records ADD COLUMN new_column TEXT DEFAULT ''");
}
sqlite3_finalize(stmt);
```

#### 修改表结构（需要重建）

```sql
-- 1. 创建新表
CREATE TABLE records_new (...);
-- 2. 复制数据
INSERT INTO records_new SELECT * FROM records;
-- 3. 删除旧表
DROP TABLE records;
-- 4. 重命名
ALTER TABLE records_new RENAME TO records;
```

### 变更后的检查清单

- [ ] `Database::initialize()` 能处理空数据库（新建表）
- [ ] `Database::initialize()` 能处理已有旧表（迁移/补字段）
- [ ] 所有现有 API 功能正常（回归测试）
- [ ] 旧数据文件可正常打开（向前兼容）
