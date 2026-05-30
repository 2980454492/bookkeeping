#include "handlers.h"

#include <json.hpp>
#include <sstream>
#include <iostream>

using json = nlohmann::json;

// ── 工具函数：解析通用查询参数 ──────────────────────────────────────

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

// ── JSON 序列化 / 反序列化 ──────────────────────────────────────────

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
    return j;
}

static json errorJson(const std::string& msg) {
    return {{"error", msg}};
}

// ── 路由注册 ─────────────────────────────────────────────────────────

void registerRoutes(httplib::Server& svr, Database& db) {

    // ── 健康检查 ──────────────────────────────────────────────────
    svr.Get("/api/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("{\"status\":\"ok\"}", "application/json");
    });

    // ── 记录相关接口 ──────────────────────────────────────────────

    // GET /api/records — 查询记录列表（支持筛选条件）
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

    // GET /api/records/:id — 获取单条记录
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

    // POST /api/records — 新增记录
    svr.Post("/api/records", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            auto j = json::parse(req.body);
            Record r = jsonToRecord(j);

            // 校验必填字段
            if (r.datetime.empty() || r.type.empty() || r.category_l1.empty()) {
                res.status = 400;
                res.set_content(errorJson("缺少必填字段: datetime, type, category_l1").dump(),
                                "application/json");
                return;
            }
            if (r.type != "income" && r.type != "expense") {
                res.status = 400;
                res.set_content(errorJson("type 字段必须是 'income' 或 'expense'").dump(), "application/json");
                return;
            }
            if (r.amount <= 0) {
                res.status = 400;
                res.set_content(errorJson("金额必须大于 0").dump(), "application/json");
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

    // PUT /api/records/:id — 更新记录
    svr.Put(R"(/api/records/(\d+))", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            int id = std::stoi(req.matches[1]);
            auto j = json::parse(req.body);
            Record r = jsonToRecord(j);

            if (!db.updateRecord(id, r)) {
                res.status = 404;
                res.set_content(errorJson("记录未找到或更新失败").dump(), "application/json");
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

    // DELETE /api/records/:id — 删除记录
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

    // ── 分类相关接口 ──────────────────────────────────────────────

    // GET /api/categories — 获取分类列表
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

    // POST /api/categories/l1 — 新增一级分类
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
            res.status = 201;
            res.set_content(categoryL1ToJson(cat).dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(errorJson(e.what()).dump(), "application/json");
        }
    });

    // DELETE /api/categories/l1/:id — 删除一级分类
    svr.Delete(R"(/api/categories/l1/(\d+))", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            int id = std::stoi(req.matches[1]);
            db.deleteCategoryL1(id);
            res.set_content("{\"success\":true}", "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(errorJson(e.what()).dump(), "application/json");
        }
    });

    // POST /api/categories/reset — 恢复默认分类
    svr.Post("/api/categories/reset", [&](const httplib::Request&, httplib::Response& res) {
        try {
            db.resetCategories("categories.json");
            res.set_content("{\"success\":true}", "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(errorJson(e.what()).dump(), "application/json");
        }
    });

    std::cout << "[HTTP] 路由注册完毕。" << std::endl;
}
