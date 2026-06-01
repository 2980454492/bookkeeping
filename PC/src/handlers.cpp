// handlers.cpp — REST API 实现（cpp-httplib）
//
// 约定：
//   - 成功响应：application/json
//   - 业务/解析错误：{"error":"..."}，HTTP 4xx/5xx
//   - 路由 lambda 以 [&] 捕获 db，须在 Server 存活期内有效
//
// 已注册端点一览：
//   GET    /api/health
//   GET    /api/records              ?keyword&type&cat_l1&...&page&page_size
//   GET    /api/records/:id
//   POST   /api/records              body: Record JSON
//   PUT    /api/records/:id
//   DELETE /api/records/:id
//   GET    /api/categories           ?type=income|expense
//   POST   /api/categories/l1
//   DELETE /api/categories/l1/:id
//   POST   /api/categories/reset     从 cwd 下 categories.json 恢复（路径见注）
//   POST   /api/records/export       按筛选条件导出到应用根目录
//   POST   /api/records/import       按格式导入记录（全量通过才写入）

#include "handlers.h"
#include "export_util.h"
#include "import_util.h"

#include <json.hpp>
#include <iostream>
#include <cctype>

using json = nlohmann::json;

// ── 查询参数 → RecordQuery ─────────────────────────────────────────

static void fillRecordQueryFromJson(RecordQuery& q, const json& j) {
    if (j.contains("keyword") && j["keyword"].is_string())
        q.keyword = j["keyword"].get<std::string>();
    if (j.contains("type") && j["type"].is_string())
        q.type = j["type"].get<std::string>();
    if (j.contains("cat_l1") && j["cat_l1"].is_string())
        q.cat_l1 = j["cat_l1"].get<std::string>();
    if (j.contains("cat_l2") && j["cat_l2"].is_string())
        q.cat_l2 = j["cat_l2"].get<std::string>();
    if (j.contains("date_from") && j["date_from"].is_string())
        q.date_from = j["date_from"].get<std::string>();
    if (j.contains("date_to") && j["date_to"].is_string())
        q.date_to = j["date_to"].get<std::string>();
    if (j.contains("amount_min")) {
        if (j["amount_min"].is_number())
            q.amount_min = j["amount_min"].get<double>();
        else if (j["amount_min"].is_string() && !j["amount_min"].get<std::string>().empty())
            q.amount_min = std::stod(j["amount_min"].get<std::string>());
    }
    if (j.contains("amount_max")) {
        if (j["amount_max"].is_number())
            q.amount_max = j["amount_max"].get<double>();
        else if (j["amount_max"].is_string() && !j["amount_max"].get<std::string>().empty())
            q.amount_max = std::stod(j["amount_max"].get<std::string>());
    }
    if (j.contains("sort_by") && j["sort_by"].is_string())
        q.sort_by = j["sort_by"].get<std::string>();
    if (j.contains("sort_order") && j["sort_order"].is_string())
        q.sort_order = j["sort_order"].get<std::string>();
    if (j.contains("page") && j["page"].is_number_integer())
        q.page = j["page"].get<int>();
    if (j.contains("page_size") && j["page_size"].is_number_integer())
        q.page_size = j["page_size"].get<int>();
}

static RecordQuery parseRecordQuery(const httplib::Request& req) {
    RecordQuery q;
    if (req.has_param("keyword"))      q.keyword    = req.get_param_value("keyword");
    if (req.has_param("type"))         q.type       = req.get_param_value("type");
    if (req.has_param("cat_l1"))       q.cat_l1     = req.get_param_value("cat_l1");
    if (req.has_param("cat_l2"))       q.cat_l2     = req.get_param_value("cat_l2");
    if (req.has_param("date_from"))    q.date_from  = req.get_param_value("date_from");
    if (req.has_param("date_to"))      q.date_to    = req.get_param_value("date_to");
    if (req.has_param("amount_min"))   q.amount_min = std::stod(req.get_param_value("amount_min"));
    if (req.has_param("amount_max"))   q.amount_max = std::stod(req.get_param_value("amount_max"));
    if (req.has_param("sort_by"))      q.sort_by    = req.get_param_value("sort_by");
    if (req.has_param("sort_order"))   q.sort_order = req.get_param_value("sort_order");
    if (req.has_param("page"))         q.page       = std::stoi(req.get_param_value("page"));
    if (req.has_param("page_size"))    q.page_size  = std::stoi(req.get_param_value("page_size"));
    return q;
}

// ── 与前端字段对齐的 JSON 转换 ─────────────────────────────────────

static json recordToJson(const Record& r) {
    return {
        {"id", r.id},
        {"datetime", r.datetime},
        {"type", r.type},
        {"amount", r.amount},
        {"category_l1", r.category_l1},
        {"category_l2", r.category_l2},
        {"note", r.note},
        {"created_at", r.created_at},
        {"updated_at", r.updated_at}
    };
}

/** 从 POST/PUT 请求体解析；id 由 URL 提供，不在 body 中 */
static Record jsonToRecord(const json& j) {
    Record r;
    if (j.contains("datetime"))    r.datetime    = j["datetime"].get<std::string>();
    if (j.contains("type"))        r.type        = j["type"].get<std::string>();
    if (j.contains("amount"))      r.amount      = j["amount"].get<double>();
    if (j.contains("category_l1")) r.category_l1 = j["category_l1"].get<std::string>();
    if (j.contains("category_l2")) r.category_l2 = j.value("category_l2", "");
    if (j.contains("note"))        r.note        = j.value("note", "");
    return r;
}

static json categoryL1ToJson(const CategoryL1& cat) {
    json j = {
        {"id", cat.id},
        {"name", cat.name},
        {"type", cat.type},
        {"icon", cat.icon}
    };
    if (!cat.subcategories.empty()) {
        j["subcategories"] = cat.subcategories;
    }
    if (!cat.subs.empty()) {
        json subs_arr = json::array();
        for (const auto& s : cat.subs) {
            subs_arr.push_back({{"id", s.id}, {"name", s.name}});
        }
        j["subs"] = subs_arr;
    }
    return j;
}

static bool persistCategoriesFile(Database& db, const std::string& path) {
    return db.exportCategoriesToJson(path);
}

// 路由 lambda 异步执行，须保存路径副本（不能引用 main 传入的临时 string）
static std::string g_categories_json_path;
static std::string g_app_root_path;

static json errorJson(const std::string& msg) {
    return {{"error", msg}};
}

static json importErrorJson(const ImportParseResult& parsed) {
    json body = {{"error", parsed.error}};
    if (!parsed.error_code.empty()) body["error_code"] = parsed.error_code;
    if (!parsed.detail.empty()) body["detail"] = parsed.detail;
    if (!parsed.hint.empty()) body["hint"] = parsed.hint;
    if (parsed.row > 0) body["row"] = parsed.row;
    return body;
}

static void logImportFailure(const ImportParseResult& parsed) {
    std::cout << "[Import] 失败";
    if (!parsed.error_code.empty()) std::cout << " [" << parsed.error_code << "]";
    std::cout << ": " << parsed.error;
    if (!parsed.detail.empty()) std::cout << " | " << parsed.detail;
    if (parsed.row > 0) std::cout << " (行 " << parsed.row << ")";
    std::cout << std::endl;
}

// ── 路由注册 ───────────────────────────────────────────────────────

void registerRoutes(httplib::Server& svr, Database& db,
                    const std::string& categories_json_path,
                    const std::string& app_root_path) {
    g_categories_json_path = categories_json_path;
    g_app_root_path = app_root_path;

    // ── 健康检查（前端或监控可用来判断服务是否存活）────────────
    svr.Get("/api/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("{\"status\":\"ok\"}", "application/json");
    });

    // ── 收支记录 ─────────────────────────────────────────────────

    // GET /api/records — 分页列表 + 筛选
    svr.Get("/api/records", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            auto q = parseRecordQuery(req);
            auto result = db.queryRecords(q);

            json records_array = json::array();
            for (const auto& r : result.records) {
                records_array.push_back(recordToJson(r));
            }

            json body = {
                {"records", records_array},
                {"total", result.total},
                {"page", result.page},
                {"page_size", result.page_size}
            };
            res.set_content(body.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(errorJson(e.what()).dump(), "application/json");
        }
    });

    // POST /api/records/export — 按筛选条件导出到应用根目录
    svr.Post("/api/records/export", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            auto j = json::parse(req.body);
            std::string filename = j.value("filename", "");
            std::string format = j.value("format", "csv");
            std::string conflict_strategy = j.value("conflict_strategy", "cancel");
            if (filename.empty()) {
                res.status = 400;
                res.set_content(errorJson("filename 为必填字段").dump(), "application/json");
                return;
            }

            RecordQuery q;
            fillRecordQueryFromJson(q, j);
            q.page = 1;
            q.page_size = 1000000;

            auto result = db.queryRecords(q);
            auto export_result = exportRecordsToFile(
                g_app_root_path, filename, format, conflict_strategy, result.records);

            if (!export_result.ok) {
                if (export_result.error == "文件已存在") {
                    res.status = 409;
                    res.set_content(json{
                        {"error", "文件已存在"},
                        {"error_code", "file_exists"}
                    }.dump(), "application/json");
                    return;
                }
                res.status = 400;
                res.set_content(errorJson(export_result.error).dump(), "application/json");
                return;
            }

            json body = {
                {"ok", true},
                {"filename", export_result.filename},
                {"path", export_result.filepath.string()},
                {"count", export_result.count},
                {"total_matched", result.total}
            };
            res.set_content(body.dump(), "application/json");
            std::cout << "[Export] 已导出 " << export_result.count << " 条 → "
                      << export_result.filepath << std::endl;
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(errorJson(e.what()).dump(), "application/json");
        }
    });

    // POST /api/records/import — 导入导出同格式文件；全部解析成功后再批量写库
    svr.Post("/api/records/import", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            auto j = json::parse(req.body);
            std::string format = j.value("format", "");
            std::string content = j.value("content", "");
            bool content_is_base64 = j.value("content_is_base64", false);

            if (format.empty()) {
                res.status = 400;
                res.set_content(json{
                    {"error", "未指定导入格式"},
                    {"error_code", "missing_format"},
                    {"hint", "请在请求中提供 format：csv、xlsx、txt、json 或 tsv"}
                }.dump(), "application/json");
                std::cout << "[Import] 失败 [missing_format]: 未指定导入格式" << std::endl;
                return;
            }

            auto parsed = parseImportedRecords(format, content, content_is_base64);
            if (!parsed.ok) {
                res.status = 400;
                logImportFailure(parsed);
                res.set_content(importErrorJson(parsed).dump(), "application/json");
                return;
            }
            if (parsed.records.empty()) {
                res.status = 400;
                ImportParseResult empty_result;
                empty_result.error_code = "no_records";
                empty_result.error = "未解析到可导入的记录";
                empty_result.detail = "表头已识别，但无有效数据行";
                empty_result.hint = "请确认文件中除表头外还有收支记录";
                logImportFailure(empty_result);
                res.set_content(importErrorJson(empty_result).dump(), "application/json");
                return;
            }

            ImportCategoryStats cat_stats;
            if (!db.ensureCategoriesForRecords(parsed.records, cat_stats)) {
                res.status = 500;
                res.set_content(json{
                    {"error", "导入时创建分类失败"},
                    {"error_code", "category_create_failed"},
                    {"hint", "请查看服务端日志后重试"}
                }.dump(), "application/json");
                std::cout << "[Import] 失败 [category_create_failed]" << std::endl;
                return;
            }
            if (cat_stats.created_l1 > 0 || cat_stats.created_l2 > 0) {
                if (!persistCategoriesFile(db, g_categories_json_path)) {
                    res.status = 500;
                    res.set_content(json{
                        {"error", "分类已创建但写入 categories.json 失败"},
                        {"error_code", "category_persist_failed"},
                        {"detail", "新建一级 " + std::to_string(cat_stats.created_l1)
                            + " 个，二级 " + std::to_string(cat_stats.created_l2) + " 个"},
                        {"hint", "记录尚未导入，请检查应用目录写权限后重试"}
                    }.dump(), "application/json");
                    std::cout << "[Import] 失败 [category_persist_failed]" << std::endl;
                    return;
                }
            }

            int inserted = db.createRecordsBatch(parsed.records);
            if (inserted < 0) {
                res.status = 500;
                res.set_content(json{
                    {"error", "导入写入数据库失败"},
                    {"error_code", "db_write_failed"},
                    {"detail", "已解析 " + std::to_string(parsed.records.size()) + " 条，但批量写入未成功"},
                    {"hint", "请查看服务端日志或重试；若持续失败请检查数据库文件权限"}
                }.dump(), "application/json");
                std::cout << "[Import] 失败 [db_write_failed]: 批量写入失败，待写入 "
                          << parsed.records.size() << " 条" << std::endl;
                return;
            }

            json body = {
                {"ok", true},
                {"count", inserted},
                {"categories_created_l1", cat_stats.created_l1},
                {"categories_created_l2", cat_stats.created_l2}
            };
            res.set_content(body.dump(), "application/json");
            std::cout << "[Import] 已导入 " << inserted << " 条";
            if (cat_stats.created_l1 > 0 || cat_stats.created_l2 > 0) {
                std::cout << "，新建分类：一级 " << cat_stats.created_l1
                          << " 个，二级 " << cat_stats.created_l2 << " 个";
            }
            std::cout << std::endl;
        } catch (const json::parse_error& e) {
            res.status = 400;
            res.set_content(json{
                {"error", "请求 JSON 格式无效"},
                {"error_code", "request_json_invalid"},
                {"detail", e.what()}
            }.dump(), "application/json");
            std::cout << "[Import] 失败 [request_json_invalid]: " << e.what() << std::endl;
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(json{
                {"error", "导入过程发生服务器错误"},
                {"error_code", "server_error"},
                {"detail", e.what()}
            }.dump(), "application/json");
            std::cout << "[Import] 失败 [server_error]: " << e.what() << std::endl;
        }
    });

    // GET /api/records/:id — 单条详情
    svr.Get(R"(/api/records/(\d+))", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            int id = std::stoi(req.matches[1]);
            auto record = db.getRecord(id);
            if (record) {
                res.set_content(recordToJson(*record).dump(), "application/json");
            } else {
                res.status = 404;
                res.set_content(errorJson("记录未找到").dump(), "application/json");
            }
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(errorJson(e.what()).dump(), "application/json");
        }
    });

    // POST /api/records — 新建；校验通过后返回 201 + 完整记录（含 id、时间戳）
    svr.Post("/api/records", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            auto j = json::parse(req.body);
            Record r = jsonToRecord(j);

            if (r.datetime.empty() || r.type.empty()) {
                res.status = 400;
                res.set_content(errorJson("缺少必填字段: datetime, type").dump(),
                                "application/json");
                return;
            }
            if (r.type != "income" && r.type != "expense") {
                res.status = 400;
                res.set_content(errorJson("type 字段必须是 'income' 或 'expense'").dump(), "application/json");
                return;
            }
            if (r.amount < 0) {
                res.status = 400;
                res.set_content(errorJson("金额不能为负数").dump(), "application/json");
                return;
            }

            int new_id = db.createRecord(r);
            if (new_id < 0) {
                res.status = 500;
                res.set_content(errorJson("创建记录失败").dump(), "application/json");
                return;
            }

            auto created = db.getRecord(new_id);
            res.status = 201;
            res.set_content(recordToJson(*created).dump(), "application/json");
        } catch (const json::parse_error& e) {
            res.status = 400;
            res.set_content(errorJson("JSON 格式无效: " + std::string(e.what())).dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(errorJson(e.what()).dump(), "application/json");
        }
    });

    // PUT /api/records/:id — 全量更新 body 中的字段
    svr.Put(R"(/api/records/(\d+))", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            int id = std::stoi(req.matches[1]);

            // 先确认记录存在，避免 updateRecord 对不存在 id 也返回成功而误报
            if (!db.getRecord(id)) {
                res.status = 404;
                res.set_content(errorJson("记录未找到").dump(), "application/json");
                return;
            }

            auto j = json::parse(req.body);
            Record r = jsonToRecord(j);

            // 与 POST 保持一致的字段校验
            if (r.datetime.empty() || r.type.empty()) {
                res.status = 400;
                res.set_content(errorJson("缺少必填字段: datetime, type").dump(),
                                "application/json");
                return;
            }
            if (r.type != "income" && r.type != "expense") {
                res.status = 400;
                res.set_content(errorJson("type 字段必须是 'income' 或 'expense'").dump(), "application/json");
                return;
            }
            if (r.amount < 0) {
                res.status = 400;
                res.set_content(errorJson("金额不能为负数").dump(), "application/json");
                return;
            }

            if (!db.updateRecord(id, r)) {
                res.status = 500;
                res.set_content(errorJson("更新记录失败").dump(), "application/json");
                return;
            }

            auto updated = db.getRecord(id);
            res.set_content(recordToJson(*updated).dump(), "application/json");
        } catch (const json::parse_error& e) {
            res.status = 400;
            res.set_content(errorJson("JSON 格式无效").dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(errorJson(e.what()).dump(), "application/json");
        }
    });

    // DELETE /api/records/:id
    svr.Delete(R"(/api/records/(\d+))", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            int id = std::stoi(req.matches[1]);
            if (db.deleteRecord(id)) {
                res.set_content("{\"success\":true}", "application/json");
            } else {
                res.status = 404;
                res.set_content(errorJson("记录未找到").dump(), "application/json");
            }
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(errorJson(e.what()).dump(), "application/json");
        }
    });

    // ── 分类（增删改后同步写入 categories_json_path）────────────────

    // GET /api/categories — { id, name, type, icon, subcategories?, subs? }
    svr.Get("/api/categories", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            std::string type = req.get_param_value("type");
            auto cats = db.getCategories(type);

            json arr = json::array();
            for (const auto& c : cats) {
                arr.push_back(categoryL1ToJson(c));
            }
            res.set_content(arr.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(errorJson(e.what()).dump(), "application/json");
        }
    });

    // POST /api/categories/l1 — 用户自定义一级分类
    svr.Post("/api/categories/l1", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            auto j = json::parse(req.body);
            CategoryL1 cat;
            cat.name = j.value("name", "");
            cat.type = j.value("type", "");
            cat.icon = j.value("icon", "");

            if (cat.name.empty() || cat.type.empty()) {
                res.status = 400;
                res.set_content(errorJson("name 和 type 为必填字段").dump(), "application/json");
                return;
            }

            int id = db.createCategoryL1(cat);
            cat.id = id;
            if (!persistCategoriesFile(db, g_categories_json_path)) {
                res.status = 500;
                res.set_content(errorJson("分类已创建但写入 categories.json 失败").dump(),
                                "application/json");
                return;
            }
            res.status = 201;
            res.set_content(categoryL1ToJson(cat).dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(errorJson(e.what()).dump(), "application/json");
        }
    });

    // PUT /api/categories/l1/:id
    svr.Put(R"(/api/categories/l1/(\d+))", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            int id = std::stoi(req.matches[1]);
            auto j = json::parse(req.body);
            CategoryL1 cat;
            cat.name = j.value("name", "");
            cat.type = j.value("type", "");
            cat.icon = j.value("icon", "");
            cat.sort_order = j.value("sort_order", 0);

            if (cat.name.empty() || cat.type.empty()) {
                res.status = 400;
                res.set_content(errorJson("name 和 type 为必填字段").dump(), "application/json");
                return;
            }

            if (!db.updateCategoryL1(id, cat)) {
                res.status = 500;
                res.set_content(errorJson("更新一级分类失败").dump(), "application/json");
                return;
            }
            if (!persistCategoriesFile(db, g_categories_json_path)) {
                res.status = 500;
                res.set_content(errorJson("更新成功但写入 categories.json 失败").dump(),
                                "application/json");
                return;
            }
            cat.id = id;
            res.set_content(categoryL1ToJson(cat).dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(errorJson(e.what()).dump(), "application/json");
        }
    });

    // DELETE /api/categories/l1/:id — 同时删除其下所有二级分类
    svr.Delete(R"(/api/categories/l1/(\d+))", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            int id = std::stoi(req.matches[1]);
            if (!db.deleteCategoryL1(id)) {
                res.status = 500;
                res.set_content(errorJson("删除一级分类失败").dump(), "application/json");
                return;
            }
            if (!persistCategoriesFile(db, g_categories_json_path)) {
                res.status = 500;
                res.set_content(errorJson("删除成功但写入 categories.json 失败").dump(),
                                "application/json");
                return;
            }
            res.set_content("{\"success\":true}", "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(errorJson(e.what()).dump(), "application/json");
        }
    });

    // POST /api/categories/l2
    svr.Post("/api/categories/l2", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            auto j = json::parse(req.body);
            CategoryL2 cat;
            cat.l1_id = j.value("l1_id", 0);
            cat.name = j.value("name", "");
            cat.sort_order = j.value("sort_order", 0);

            if (cat.l1_id <= 0 || cat.name.empty()) {
                res.status = 400;
                res.set_content(errorJson("l1_id 和 name 为必填字段").dump(), "application/json");
                return;
            }

            auto l1 = db.getCategoryL1ById(cat.l1_id);
            if (!l1 || l1->type != "expense") {
                res.status = 400;
                res.set_content(errorJson("仅支出分类可添加二级分类").dump(), "application/json");
                return;
            }

            int id = db.createCategoryL2(cat);
            cat.id = id;
            if (!persistCategoriesFile(db, g_categories_json_path)) {
                res.status = 500;
                res.set_content(errorJson("二级分类已创建但写入 categories.json 失败").dump(),
                                "application/json");
                return;
            }
            res.status = 201;
            res.set_content(json({{"id", cat.id}, {"l1_id", cat.l1_id}, {"name", cat.name}}).dump(),
                            "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(errorJson(e.what()).dump(), "application/json");
        }
    });

    // PUT /api/categories/l2/:id
    svr.Put(R"(/api/categories/l2/(\d+))", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            int id = std::stoi(req.matches[1]);
            auto j = json::parse(req.body);
            CategoryL2 cat;
            cat.l1_id = j.value("l1_id", 0);
            cat.name = j.value("name", "");
            cat.sort_order = j.value("sort_order", 0);

            if (cat.name.empty()) {
                res.status = 400;
                res.set_content(errorJson("name 为必填字段").dump(), "application/json");
                return;
            }

            auto l1 = db.getCategoryL1ById(cat.l1_id);
            if (!l1 || l1->type != "expense") {
                res.status = 400;
                res.set_content(errorJson("仅支出分类可设置二级分类").dump(), "application/json");
                return;
            }

            if (!db.updateCategoryL2(id, cat)) {
                res.status = 500;
                res.set_content(errorJson("更新二级分类失败").dump(), "application/json");
                return;
            }
            if (!persistCategoriesFile(db, g_categories_json_path)) {
                res.status = 500;
                res.set_content(errorJson("更新成功但写入 categories.json 失败").dump(),
                                "application/json");
                return;
            }
            cat.id = id;
            res.set_content(json({{"id", cat.id}, {"l1_id", cat.l1_id}, {"name", cat.name}}).dump(),
                            "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(errorJson(e.what()).dump(), "application/json");
        }
    });

    // DELETE /api/categories/l2/:id
    svr.Delete(R"(/api/categories/l2/(\d+))", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            int id = std::stoi(req.matches[1]);
            if (!db.deleteCategoryL2(id)) {
                res.status = 500;
                res.set_content(errorJson("删除二级分类失败").dump(), "application/json");
                return;
            }
            if (!persistCategoriesFile(db, g_categories_json_path)) {
                res.status = 500;
                res.set_content(errorJson("删除成功但写入 categories.json 失败").dump(),
                                "application/json");
                return;
            }
            res.set_content("{\"success\":true}", "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(errorJson(e.what()).dump(), "application/json");
        }
    });

    // POST /api/categories/reset — 恢复默认分类模板
    svr.Post("/api/categories/reset", [&](const httplib::Request&, httplib::Response& res) {
        try {
            if (!db.resetCategories(g_categories_json_path)) {
                res.status = 500;
                res.set_content(errorJson("恢复默认分类失败").dump(), "application/json");
                return;
            }
            res.set_content("{\"success\":true}", "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(errorJson(e.what()).dump(), "application/json");
        }
    });

    std::cout << "[HTTP] 路由注册完毕。" << std::endl;
}
