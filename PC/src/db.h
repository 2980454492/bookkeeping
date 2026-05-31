// db.h — SQLite 持久化层接口
//
// 职责：定义收支记录、分类相关的内存结构体，以及 Database 类的公开 API。
// 实现见 db.cpp；HTTP 层通过 handlers.cpp 调用本模块，不直接操作 SQL。
//
// 表设计概要：
//   category_l1 / category_l2 — 二级分类体系（收入一级可无子类）
//   records                   — 收支流水（分类以名称冗余存储，便于筛选与历史兼容）

#pragma once

#include <sqlite3.h>
#include <string>
#include <vector>
#include <optional>
#include <memory>

// ── 数据结构（与前端 JSON 字段对齐）────────────────────────────────

/** 单条收支记录，对应表 records */
struct Record {
    int id = 0;
    std::string datetime;    // "YYYY-MM-DD HH:MM"，用于排序与日期筛选
    std::string type;        // "income"（收入）或 "expense"（支出）
    double amount = 0.0;
    std::string category_l1; // 一级分类名称（存文本，非外键 id）
    std::string category_l2; // 二级分类名称，收入项通常为空
    std::string note;        // 备注，关键词搜索会匹配此字段
    std::string created_at;  // 插入时间，由数据库 DEFAULT 生成
    std::string updated_at;  // 最后修改时间，UPDATE 时刷新
};

/** 列表查询条件，由 handlers 从 URL 查询参数填充 */
struct RecordQuery {
    std::string keyword;     // 模糊匹配 note / 分类名 / 金额字符串
    std::string type;        // 按 income | expense 过滤
    std::string cat_l1;
    std::string cat_l2;
    std::string date_from;   // datetime >= date_from
    std::string date_to;     // datetime <= date_to 23:59
    double amount_min = -1;  // < 0 表示不限制下限
    double amount_max = -1;  // < 0 表示不限制上限
    std::string sort_by = "datetime";   // "datetime" | "amount"
    std::string sort_order = "desc";    // "asc" | "desc"
    int page = 1;
    int page_size = 50;
};

/** 分页查询结果：当前页数据 + 命中总数（用于前端分页器） */
struct RecordListResult {
    std::vector<Record> records;
    int total = 0;
    int page = 1;
    int page_size = 50;
};

/** 二级分类项（查询时带 id，供设置页增删改） */
struct CategorySubItem {
    int id = 0;
    std::string name;
};

/** 一级分类；subcategories / subs 仅在查询输出时填充 */
struct CategoryL1 {
    int id = 0;
    std::string name;
    std::string type;       // "income" | "expense"
    std::string icon;       // 前端展示用图标标识
    int sort_order = 0;
    std::vector<std::string> subcategories;
    std::vector<CategorySubItem> subs;
};

/** 二级分类，外键 l1_id 指向 category_l1 */
struct CategoryL2 {
    int id = 0;
    int l1_id = 0;
    std::string name;
    int sort_order = 0;
};

// ── 数据库操作类 ───────────────────────────────────────────────────

class Database {
public:
    Database() = default;
    ~Database();

    /** 打开或创建 db 文件；成功时启用 WAL、外键约束 */
    bool open(const std::string& db_path);
    void close();

    /** 建表；若 category_l1 为空则从 categories_json_path 导入默认分类 */
    bool initialize(const std::string& categories_json_path);

    // ── 收支记录 ───────────────────────────────────────────────
    RecordListResult queryRecords(const RecordQuery& q);
    std::optional<Record> getRecord(int id);
    int createRecord(const Record& r);       // 成功返回新 id，失败返回 -1
    bool updateRecord(int id, const Record& r);
    bool deleteRecord(int id);

    // ── 分类（用户可增删改；reset 从模板文件恢复）────────────
    std::vector<CategoryL1> getCategories(const std::string& type = ""); // type 空=全部
    std::optional<CategoryL1> getCategoryL1ById(int id);
    int  createCategoryL1(const CategoryL1& cat);
    bool updateCategoryL1(int id, const CategoryL1& cat);
    bool deleteCategoryL1(int id);         // 级联删除其下 category_l2

    int  createCategoryL2(const CategoryL2& cat);
    bool updateCategoryL2(int id, const CategoryL2& cat);
    bool deleteCategoryL2(int id);

    /** 清空分类表后重新从 JSON 导入（不删除 records 表数据） */
    bool resetCategories(const std::string& categories_json_path);

    /** 将当前分类导出为 categories.json 格式并写入文件 */
    bool exportCategoriesToJson(const std::string& json_path) const;

private:
    void createTables();
    bool importCategoriesFromJson(const std::string& json_path);
    void executeSql(const std::string& sql);
    std::string escape(const std::string& s) const;  // 拼接 SQL 时对单引号转义

    sqlite3* db_ = nullptr;
};
