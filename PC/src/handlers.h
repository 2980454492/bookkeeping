#pragma once

#include "db.h"
#include <httplib.h>

// 向 httplib::Server 注册全部 API 路由
void registerRoutes(httplib::Server& svr, Database& db);
