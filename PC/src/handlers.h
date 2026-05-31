// handlers.h — REST API 路由声明
//
// registerRoutes() 将 /api/* 端点绑定到 Database 操作，供 main.cpp 在启动 HTTP 服务时调用。
// 请求解析、JSON 序列化、状态码与错误体格式均在 handlers.cpp 中实现。

#pragma once

#include "db.h"
#include <httplib.h>

/** 注册全部 API 路由；db 以引用捕获，须在 svr.listen 期间保持存活 */
void registerRoutes(httplib::Server& svr, Database& db, const std::string& categories_json_path);
