#pragma once

#include "db.h"
#include <httplib.h>

// Registers all API routes on the given httplib::Server
void registerRoutes(httplib::Server& svr, Database& db);
