// db.cpp — SQLite 持久化层实现
//
// SQL 策略说明：
//   - 记录 CRUD：预编译语句（? 占位符），防注入且性能稳定
//   - 分类部分动态 SQL：使用 escape() 转义单引号后拼接（本机单用户场景）
//   - 分类名在 records 表中以 TEXT 冗余存储，改分类名不会自动回写历史记录

#include "db.h"

#include <json.hpp>
#include <fstream>
#include <sstream>
#include <cstring>
#include <iostream>

using json = nlohmann::json;

// ── 内部工具 ───────────────────────────────────────────────────────

/** 将字符串中的单引号替换为 SQL 标准的 ''，用于拼接字面量 */
std::string Database::escape(const std::string& s) const {
    std::string out;
    out.reserve(s.size() + 10);
    for (char c : s) {
        if (c == '\'') out += "''";
        else out += c;
    }
    return out;
}

/** 执行无结果集的 SQL；失败时抛 std::runtime_error（含 SQL 文本便于调试） */
void Database::executeSql(const std::string& sql) {
    char* err = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        std::string msg = err ? err : "未知错误";
        sqlite3_free(err);
        throw std::runtime_error("SQL 错误: " + msg + "\nSQL: " + sql);
    }
}

// ── 打开 / 关闭 ────────────────────────────────────────────────────

bool Database::open(const std::string& db_path) {
    int rc = sqlite3_open(db_path.c_str(), &db_);
    if (rc != SQLITE_OK) {
        std::cerr << "[DB] 无法打开数据库: " << sqlite3_errmsg(db_) << std::endl;
        return false;
    }
    // WAL：读写可并发，适合本地服务 + 浏览器多请求
    executeSql("PRAGMA journal_mode=WAL");
    // 删除一级分类时自动删除其下二级分类
    executeSql("PRAGMA foreign_keys=ON");
    std::cout << "[DB] 数据库已打开: " << db_path << std::endl;
    return true;
}

void Database::close() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
        std::cout << "[DB] 数据库已关闭。" << std::endl;
    }
}

Database::~Database() {
    close();
}

// ── 建表 ────────────────────────────────────────────────────────────

void Database::createTables() {
    // 一级分类：支出/收入分开，type 由 CHECK 约束限定
    executeSql(R"(
        CREATE TABLE IF NOT EXISTS category_l1 (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            name        TEXT NOT NULL,
            type        TEXT NOT NULL CHECK(type IN ('income','expense')),
            icon        TEXT DEFAULT '',
            sort_order  INTEGER DEFAULT 0,
            created_at  TEXT DEFAULT (datetime('now','localtime'))
        )
    )");

    // 二级分类：l1_id 外键，ON DELETE CASCADE
    executeSql(R"(
        CREATE TABLE IF NOT EXISTS category_l2 (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            l1_id       INTEGER NOT NULL REFERENCES category_l1(id) ON DELETE CASCADE,
            name        TEXT NOT NULL,
            sort_order  INTEGER DEFAULT 0,
            created_at  TEXT DEFAULT (datetime('now','localtime'))
        )
    )");

    // 收支流水：分类字段存名称快照，与 category_* 表无外键关联
    executeSql(R"(
        CREATE TABLE IF NOT EXISTS records (
            id              INTEGER PRIMARY KEY AUTOINCREMENT,
            datetime        TEXT NOT NULL,
            type            TEXT NOT NULL CHECK(type IN ('income','expense')),
            amount          REAL NOT NULL,
            category_l1     TEXT NOT NULL,
            category_l2     TEXT DEFAULT '',
            note            TEXT DEFAULT '',
            created_at      TEXT DEFAULT (datetime('now','localtime')),
            updated_at      TEXT DEFAULT (datetime('now','localtime'))
        )
    )");

    executeSql("CREATE INDEX IF NOT EXISTS idx_records_datetime ON records(datetime)");
    executeSql("CREATE INDEX IF NOT EXISTS idx_records_type ON records(type)");
    executeSql("CREATE INDEX IF NOT EXISTS idx_records_cat1 ON records(category_l1)");

    std::cout << "[DB] 数据表已创建 / 已验证。" << std::endl;
}

// ── 从 categories.json 导入默认分类 ───────────────────────────────
// JSON 结构：{ "expense": [{ name, icon, subcategories: [...] }], "income": [...] }

bool Database::importCategoriesFromJson(const std::string& json_path) {
    std::ifstream f(json_path);
    if (!f.is_open()) {
        std::cerr << "[DB] 无法打开分类模板文件: " << json_path << std::endl;
        return false;
    }
    json j;
    try {
        f >> j;
    } catch (const std::exception& e) {
        std::cerr << "[DB] JSON 解析错误: " << e.what() << std::endl;
        return false;
    }

    int sort = 0;

    // 支出：每个一级下可挂多个二级
    if (j.contains("expense") && j["expense"].is_array()) {
        for (const auto& cat : j["expense"]) {
            std::string name = cat.value("name", "");
            std::string icon = cat.value("icon", "");
            if (name.empty()) continue;

            std::string sql = "INSERT INTO category_l1 (name, type, icon, sort_order) VALUES ('"
                            + escape(name) + "', 'expense', '" + escape(icon) + "', " + std::to_string(sort++) + ")";
            executeSql(sql);

            int l1_id = static_cast<int>(sqlite3_last_insert_rowid(db_));

            if (cat.contains("subcategories") && cat["subcategories"].is_array()) {
                int l2_sort = 0;
                for (const auto& sub : cat["subcategories"]) {
                    std::string sub_name = sub.get<std::string>();
                    std::string sql2 = "INSERT INTO category_l2 (l1_id, name, sort_order) VALUES ("
                                     + std::to_string(l1_id) + ", '" + escape(sub_name) + "', "
                                     + std::to_string(l2_sort++) + ")";
                    executeSql(sql2);
                }
            }
        }
    }

    // 收入：模板中通常只有一级，无 subcategories 数组
    sort = 0;
    if (j.contains("income") && j["income"].is_array()) {
        for (const auto& cat : j["income"]) {
            std::string name = cat.value("name", "");
            std::string icon = cat.value("icon", "");
            if (name.empty()) continue;

            std::string sql = "INSERT INTO category_l1 (name, type, icon, sort_order) VALUES ('"
                            + escape(name) + "', 'income', '" + escape(icon) + "', " + std::to_string(sort++) + ")";
            executeSql(sql);
        }
    }

    std::cout << "[DB] 分类已从 " << json_path << " 导入" << std::endl;
    return true;
}

/** 清空分类表后重新导入；已有收支记录保留，仅分类元数据被重置 */
bool Database::resetCategories(const std::string& json_path) {
    executeSql("DELETE FROM category_l2");
    executeSql("DELETE FROM category_l1");
    return importCategoriesFromJson(json_path);
}

// ── 初始化（main 启动时调用一次）──────────────────────────────────

bool Database::initialize(const std::string& categories_json_path) {
    createTables();

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM category_l1", -1, &stmt, nullptr);
    int count = 0;
    if (stmt) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }

    // 仅首次（或用户清空分类后）导入，避免覆盖用户自定义分类
    if (count == 0) {
        std::cout << "[DB] 分类表为空，正在导入默认分类..." << std::endl;
        return importCategoriesFromJson(categories_json_path);
    }
    std::cout << "[DB] 分类已存在（" << count << " 条记录）。" << std::endl;
    return true;
}

// ── 记录：分页列表查询 ─────────────────────────────────────────────

RecordListResult Database::queryRecords(const RecordQuery& q) {
    RecordListResult result;
    result.page = q.page;
    result.page_size = q.page_size;

    // 动态 WHERE：空字段表示不参与筛选
    std::ostringstream where_clause;
    where_clause << "WHERE 1=1";

    if (!q.keyword.empty()) {
        std::string kw = escape(q.keyword);
        where_clause << " AND (note LIKE '%" << kw << "%'"
                     << " OR category_l1 LIKE '%" << kw << "%'"
                     << " OR category_l2 LIKE '%" << kw << "%'"
                     << " OR CAST(amount AS TEXT) LIKE '%" << kw << "%')";
    }
    if (!q.type.empty()) {
        where_clause << " AND type='" << escape(q.type) << "'";
    }
    if (!q.cat_l1.empty()) {
        where_clause << " AND category_l1='" << escape(q.cat_l1) << "'";
    }
    if (!q.cat_l2.empty()) {
        where_clause << " AND category_l2='" << escape(q.cat_l2) << "'";
    }
    if (!q.date_from.empty()) {
        where_clause << " AND datetime >= '" << escape(q.date_from) << "'";
    }
    if (!q.date_to.empty()) {
        // 含当天全天：上限拼到 23:59
        where_clause << " AND datetime <= '" << escape(q.date_to) << " 23:59'";
    }
    if (q.amount_min >= 0) {
        where_clause << " AND amount >= " << q.amount_min;
    }
    if (q.amount_max >= 0) {
        where_clause << " AND amount <= " << q.amount_max;
    }

    // 先 COUNT 再 LIMIT/OFFSET，保证 total 与当前筛选一致
    std::string count_sql = "SELECT COUNT(*) FROM records " + where_clause.str();
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, count_sql.c_str(), -1, &stmt, nullptr);
    if (stmt && sqlite3_step(stmt) == SQLITE_ROW) {
        result.total = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);

    std::string sort_col = (q.sort_by == "amount") ? "amount" : "datetime";
    std::string sort_dir = (q.sort_order == "asc") ? "ASC" : "DESC";
    int offset = (q.page - 1) * q.page_size;

    std::ostringstream query_sql;
    query_sql << "SELECT id, datetime, type, amount, category_l1, category_l2, note, created_at, updated_at "
              << "FROM records " << where_clause.str()
              << " ORDER BY " << sort_col << " " << sort_dir
              << " LIMIT " << q.page_size << " OFFSET " << offset;

    sqlite3_prepare_v2(db_, query_sql.str().c_str(), -1, &stmt, nullptr);
    if (!stmt) {
        std::cerr << "[DB] 查询错误: " << sqlite3_errmsg(db_) << std::endl;
        return result;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Record r;
        r.id = sqlite3_column_int(stmt, 0);
        const char* dt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        if (dt) r.datetime = dt;
        const char* tp = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        if (tp) r.type = tp;
        r.amount = sqlite3_column_double(stmt, 3);
        const char* c1 = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        if (c1) r.category_l1 = c1;
        const char* c2 = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        if (c2) r.category_l2 = c2;
        const char* nt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        if (nt) r.note = nt;
        const char* ca = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        if (ca) r.created_at = ca;
        const char* ua = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
        if (ua) r.updated_at = ua;
        result.records.push_back(std::move(r));
    }
    sqlite3_finalize(stmt);

    return result;
}

std::optional<Record> Database::getRecord(int id) {
    const char* sql = "SELECT id, datetime, type, amount, category_l1, category_l2, note, created_at, updated_at "
                      "FROM records WHERE id=?";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (!stmt) return std::nullopt;

    sqlite3_bind_int(stmt, 1, id);
    std::optional<Record> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        Record r;
        r.id = sqlite3_column_int(stmt, 0);
        const char* dt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        if (dt) r.datetime = dt;
        const char* tp = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        if (tp) r.type = tp;
        r.amount = sqlite3_column_double(stmt, 3);
        const char* c1 = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        if (c1) r.category_l1 = c1;
        const char* c2 = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        if (c2) r.category_l2 = c2;
        const char* nt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        if (nt) r.note = nt;
        const char* ca = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        if (ca) r.created_at = ca;
        const char* ua = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
        if (ua) r.updated_at = ua;
        result = std::move(r);
    }
    sqlite3_finalize(stmt);
    return result;
}

int Database::createRecord(const Record& r) {
    const char* sql = "INSERT INTO records (datetime, type, amount, category_l1, category_l2, note) "
                      "VALUES (?, ?, ?, ?, ?, ?)";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (!stmt) return -1;

    sqlite3_bind_text(stmt, 1, r.datetime.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, r.type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 3, r.amount);
    sqlite3_bind_text(stmt, 4, r.category_l1.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, r.category_l2.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, r.note.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        std::cerr << "[DB] 插入错误: " << sqlite3_errmsg(db_) << std::endl;
        sqlite3_finalize(stmt);
        return -1;
    }
    sqlite3_finalize(stmt);
    int new_id = static_cast<int>(sqlite3_last_insert_rowid(db_));
    std::cout << "[DB] 记录已创建, id=" << new_id << std::endl;
    return new_id;
}

bool Database::updateRecord(int id, const Record& r) {
    const char* sql = "UPDATE records SET datetime=?, type=?, amount=?, category_l1=?, "
                      "category_l2=?, note=?, updated_at=datetime('now','localtime') WHERE id=?";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (!stmt) return false;

    sqlite3_bind_text(stmt, 1, r.datetime.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, r.type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 3, r.amount);
    sqlite3_bind_text(stmt, 4, r.category_l1.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, r.category_l2.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, r.note.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 7, id);

    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    if (!ok) std::cerr << "[DB] 更新错误: " << sqlite3_errmsg(db_) << std::endl;
    sqlite3_finalize(stmt);
    return ok;
}

bool Database::deleteRecord(int id) {
    const char* sql = "DELETE FROM records WHERE id=?";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (!stmt) return false;
    sqlite3_bind_int(stmt, 1, id);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    if (ok) std::cout << "[DB] 记录 " << id << " 已删除。" << std::endl;
    return ok;
}

// ── 分类查询与管理 ─────────────────────────────────────────────────

std::vector<CategoryL1> Database::getCategories(const std::string& type) {
    std::vector<CategoryL1> result;

    std::string sql = "SELECT id, name, type, icon, sort_order FROM category_l1";
    if (!type.empty()) {
        sql += " WHERE type='" + escape(type) + "'";
    }
    sql += " ORDER BY sort_order, id";

    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (!stmt) return result;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        CategoryL1 cat;
        cat.id = sqlite3_column_int(stmt, 0);
        const char* nm = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        if (nm) cat.name = nm;
        const char* tp = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        if (tp) cat.type = tp;
        const char* ic = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        if (ic) cat.icon = ic;
        cat.sort_order = sqlite3_column_int(stmt, 4);

        // N+1 查询：每个一级拉取其二级名称列表（分类数量小，可接受）
        sqlite3_stmt* stmt2 = nullptr;
        sqlite3_prepare_v2(db_, "SELECT name FROM category_l2 WHERE l1_id=? ORDER BY sort_order, id",
                           -1, &stmt2, nullptr);
        if (stmt2) {
            sqlite3_bind_int(stmt2, 1, cat.id);
            while (sqlite3_step(stmt2) == SQLITE_ROW) {
                const char* sub = reinterpret_cast<const char*>(sqlite3_column_text(stmt2, 0));
                if (sub) cat.subcategories.push_back(sub);
            }
            sqlite3_finalize(stmt2);
        }
        result.push_back(std::move(cat));
    }
    sqlite3_finalize(stmt);
    return result;
}

int Database::createCategoryL1(const CategoryL1& cat) {
    std::string sql = "INSERT INTO category_l1 (name, type, icon, sort_order) VALUES ('"
                    + escape(cat.name) + "', '" + escape(cat.type) + "', '"
                    + escape(cat.icon) + "', " + std::to_string(cat.sort_order) + ")";
    executeSql(sql);
    return static_cast<int>(sqlite3_last_insert_rowid(db_));
}

bool Database::updateCategoryL1(int id, const CategoryL1& cat) {
    std::string sql = "UPDATE category_l1 SET name='" + escape(cat.name)
                    + "', type='" + escape(cat.type)
                    + "', icon='" + escape(cat.icon)
                    + "', sort_order=" + std::to_string(cat.sort_order)
                    + " WHERE id=" + std::to_string(id);
    try {
        executeSql(sql);
        return true;
    } catch (...) {
        return false;
    }
}

bool Database::deleteCategoryL1(int id) {
    try {
        executeSql("DELETE FROM category_l2 WHERE l1_id=" + std::to_string(id));
        executeSql("DELETE FROM category_l1 WHERE id=" + std::to_string(id));
        return true;
    } catch (...) {
        return false;
    }
}

int Database::createCategoryL2(const CategoryL2& cat) {
    std::string sql = "INSERT INTO category_l2 (l1_id, name, sort_order) VALUES ("
                    + std::to_string(cat.l1_id) + ", '" + escape(cat.name) + "', "
                    + std::to_string(cat.sort_order) + ")";
    executeSql(sql);
    return static_cast<int>(sqlite3_last_insert_rowid(db_));
}

bool Database::updateCategoryL2(int id, const CategoryL2& cat) {
    std::string sql = "UPDATE category_l2 SET name='" + escape(cat.name)
                    + "', sort_order=" + std::to_string(cat.sort_order)
                    + ", l1_id=" + std::to_string(cat.l1_id)
                    + " WHERE id=" + std::to_string(id);
    try {
        executeSql(sql);
        return true;
    } catch (...) {
        return false;
    }
}

bool Database::deleteCategoryL2(int id) {
    try {
        executeSql("DELETE FROM category_l2 WHERE id=" + std::to_string(id));
        return true;
    } catch (...) {
        return false;
    }
}
