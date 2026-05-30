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

#include "handlers.h"

#include <json.hpp>
#include <sstream>
#include <iostream>

using json = nlohmann::json;

// ── 查询参数 → RecordQuery ─────────────────────────────────────────

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
    return j;
}

static json errorJson(const std::string& msg) {
    return {{"error", msg}};
}

// ── 路由注册 ───────────────────────────────────────────────────────

void registerRoutes(httplib::Server& svr, Database& db) {

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

    // PUT /api/records/:id — 全量更新 body 中的字段
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

    // ── 分类 ─────────────────────────────────────────────────────
    // 注：二级分类的增删改 API 未暴露，仅通过 reset 与 DB 层 createCategoryL2 等内部使用

    // GET /api/categories — 树形扁平为 { id, name, type, icon, subcategories? }
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
            res.status = 201;
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
            db.deleteCategoryL1(id);
            res.set_content("{\"success\":true}", "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(errorJson(e.what()).dump(), "application/json");
        }
    });

    // POST /api/categories/reset — 恢复默认分类模板
    // 路径为相对 cwd 的 "categories.json"；run.sh 在 build/ 下启动时通常能命中 POST_BUILD 复制的文件
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
