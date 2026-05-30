#pragma once

#include <sqlite3.h>
#include <string>
#include <vector>
#include <optional>
#include <memory>

// ── 数据结构 ──────────────────────────────────────────────────────────

struct Record {
    int id = 0;
    std::string datetime;    // "YYYY-MM-DD HH:MM"
    std::string type;        // "income"（收入）或 "expense"（支出）
    double amount = 0.0;
    std::string category_l1; // 一级分类名称
    std::string category_l2; // 二级分类名称
    std::string note;        // 备注
    std::string created_at;
    std::string updated_at;
};

struct RecordQuery {
    std::string keyword;
    std::string type;
    std::string cat_l1;
    std::string cat_l2;
    std::string date_from;
    std::string date_to;
    double amount_min = -1;
    double amount_max = -1;
    std::string sort_by = "datetime";
    std::string sort_order = "desc";
    int page = 1;
    int page_size = 50;
};

struct RecordListResult {
    std::vector<Record> records;
    int total = 0;
    int page = 1;
    int page_size = 50;
};

struct CategoryL1 {
    int id = 0;
    std::string name;
    std::string type;       // "income"（收入）或 "expense"（支出）
    std::string icon;
    int sort_order = 0;
    std::vector<std::string> subcategories; // 仅输出时使用，存放二级分类名称列表
};

struct CategoryL2 {
    int id = 0;
    int l1_id = 0;
    std::string name;
    int sort_order = 0;
};

// ── 数据库操作类 ──────────────────────────────────────────────────────

class Database {
public:
    Database() = default;
    ~Database();

    // 打开 / 关闭数据库
    bool open(const std::string& db_path);
    void close();

    // 建表并从 JSON 文件导入默认分类
    bool initialize(const std::string& categories_json_path);

    // ── 记录 CRUD ─────────────────────────────────────────────
    RecordListResult queryRecords(const RecordQuery& q);
    std::optional<Record> getRecord(int id);
    int createRecord(const Record& r);
    bool updateRecord(int id, const Record& r);
    bool deleteRecord(int id);

    // ── 分类管理 ──────────────────────────────────────────────
    std::vector<CategoryL1> getCategories(const std::string& type = "");
    int  createCategoryL1(const CategoryL1& cat);
    bool updateCategoryL1(int id, const CategoryL1& cat);
    bool deleteCategoryL1(int id);

    int  createCategoryL2(const CategoryL2& cat);
    bool updateCategoryL2(int id, const CategoryL2& cat);
    bool deleteCategoryL2(int id);

    bool resetCategories(const std::string& categories_json_path);

private:
    void createTables();
    bool importCategoriesFromJson(const std::string& json_path);
    void executeSql(const std::string& sql);
    std::string escape(const std::string& s) const;

    sqlite3* db_ = nullptr;
};
