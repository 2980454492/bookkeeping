#pragma once

#include <sqlite3.h>
#include <string>
#include <vector>
#include <optional>
#include <memory>

// ── Data structures ──────────────────────────────────────────────────

struct Record {
    int id = 0;
    std::string datetime;   // "YYYY-MM-DD HH:MM"
    std::string type;       // "income" or "expense"
    double amount = 0.0;
    std::string category_l1;
    std::string category_l2; // empty for income
    std::string note;
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
    std::string type;       // "income" or "expense"
    std::string icon;
    int sort_order = 0;
    std::vector<std::string> subcategories; // for output only
};

struct CategoryL2 {
    int id = 0;
    int l1_id = 0;
    std::string name;
    int sort_order = 0;
};

// ── Database class ───────────────────────────────────────────────────

class Database {
public:
    Database() = default;
    ~Database();

    // Open / close
    bool open(const std::string& db_path);
    void close();

    // Initialize tables and import default categories from JSON
    bool initialize(const std::string& categories_json_path);

    // ── Records CRUD ─────────────────────────────────────────
    RecordListResult queryRecords(const RecordQuery& q);
    std::optional<Record> getRecord(int id);
    int createRecord(const Record& r);
    bool updateRecord(int id, const Record& r);
    bool deleteRecord(int id);

    // ── Categories ───────────────────────────────────────────
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
