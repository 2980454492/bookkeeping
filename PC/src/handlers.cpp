#include "handlers.h"

#include <json.hpp>
#include <sstream>
#include <iostream>

using json = nlohmann::json;

// ── Helper: parse common query parameters ──────────────────────────

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

// ── Route registration ─────────────────────────────────────────────

void registerRoutes(httplib::Server& svr, Database& db) {

    // ── Health check ─────────────────────────────────────────────
    svr.Get("/api/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("{\"status\":\"ok\"}", "application/json");
    });

    // ── Records ──────────────────────────────────────────────────

    // GET /api/records — list records with filters
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

    // GET /api/records/:id — get single record
    svr.Get(R"(/api/records/(\d+))", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            int id = std::stoi(req.matches[1]);
            auto record = db.getRecord(id);
            if (record) {
                res.set_content(recordToJson(*record).dump(), "application/json");
            } else {
                res.status = 404;
                res.set_content(errorJson("Record not found").dump(), "application/json");
            }
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(errorJson(e.what()).dump(), "application/json");
        }
    });

    // POST /api/records — create record
    svr.Post("/api/records", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            auto j = json::parse(req.body);
            Record r = jsonToRecord(j);

            // Validate required fields
            if (r.datetime.empty() || r.type.empty() || r.category_l1.empty()) {
                res.status = 400;
                res.set_content(errorJson("Missing required fields: datetime, type, category_l1").dump(),
                                "application/json");
                return;
            }
            if (r.type != "income" && r.type != "expense") {
                res.status = 400;
                res.set_content(errorJson("type must be 'income' or 'expense'").dump(), "application/json");
                return;
            }
            if (r.amount <= 0) {
                res.status = 400;
                res.set_content(errorJson("amount must be positive").dump(), "application/json");
                return;
            }

            int new_id = db.createRecord(r);
            if (new_id < 0) {
                res.status = 500;
                res.set_content(errorJson("Failed to create record").dump(), "application/json");
                return;
            }

            auto created = db.getRecord(new_id);
            res.status = 201;
            res.set_content(recordToJson(*created).dump(), "application/json");
        } catch (const json::parse_error& e) {
            res.status = 400;
            res.set_content(errorJson("Invalid JSON: " + std::string(e.what())).dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(errorJson(e.what()).dump(), "application/json");
        }
    });

    // PUT /api/records/:id — update record
    svr.Put(R"(/api/records/(\d+))", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            int id = std::stoi(req.matches[1]);
            auto j = json::parse(req.body);
            Record r = jsonToRecord(j);

            if (!db.updateRecord(id, r)) {
                res.status = 404;
                res.set_content(errorJson("Record not found or update failed").dump(), "application/json");
                return;
            }

            auto updated = db.getRecord(id);
            res.set_content(recordToJson(*updated).dump(), "application/json");
        } catch (const json::parse_error& e) {
            res.status = 400;
            res.set_content(errorJson("Invalid JSON").dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(errorJson(e.what()).dump(), "application/json");
        }
    });

    // DELETE /api/records/:id — delete record
    svr.Delete(R"(/api/records/(\d+))", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            int id = std::stoi(req.matches[1]);
            if (db.deleteRecord(id)) {
                res.set_content("{\"success\":true}", "application/json");
            } else {
                res.status = 404;
                res.set_content(errorJson("Record not found").dump(), "application/json");
            }
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(errorJson(e.what()).dump(), "application/json");
        }
    });

    // ── Categories ───────────────────────────────────────────────

    // GET /api/categories — list categories
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

    // POST /api/categories/l1 — add L1 category
    svr.Post("/api/categories/l1", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            auto j = json::parse(req.body);
            CategoryL1 cat;
            cat.name = j.value("name", "");
            cat.type = j.value("type", "");
            cat.icon = j.value("icon", "");

            if (cat.name.empty() || cat.type.empty()) {
                res.status = 400;
                res.set_content(errorJson("name and type are required").dump(), "application/json");
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

    // DELETE /api/categories/l1/:id
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

    // POST /api/categories/reset — reset to defaults
    svr.Post("/api/categories/reset", [&](const httplib::Request&, httplib::Response& res) {
        try {
            db.resetCategories("categories.json");
            res.set_content("{\"success\":true}", "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(errorJson(e.what()).dump(), "application/json");
        }
    });

    std::cout << "[HTTP] Routes registered." << std::endl;
}
