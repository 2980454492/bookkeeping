#include "db.h"
#include "handlers.h"

#include <httplib.h>
#include <iostream>
#include <string>
#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;

// ── Find the project root (where categories.json and frontend/ live) ─
// During development: look relative to current working directory
// In production: look relative to the executable path
static fs::path findRoot() {
    // 1. Try current working directory (works for `cd build && ./bookkeeping`)
    fs::path cwd = fs::current_path();
    if (fs::exists(cwd / "categories.json") && fs::exists(cwd / "frontend")) {
        return cwd;
    }

    // 2. Try the executable's directory
    char exe_path[4096] = {};
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len > 0) {
        fs::path exe_dir = fs::path(std::string(exe_path, len)).parent_path();
        if (fs::exists(exe_dir / "categories.json") && fs::exists(exe_dir / "frontend")) {
            return exe_dir;
        }
    }

    // 3. Fallback: parent of build directory (for CMake out-of-source builds)
    if (cwd.filename() == "build") {
        fs::path parent = cwd.parent_path();
        if (fs::exists(parent / "categories.json") && fs::exists(parent / "frontend")) {
            return parent;
        }
    }

    return cwd; // last resort
}

// ── Get data directory for SQLite database ─────────────────────────
static fs::path getDataDir(const fs::path& root) {
    // Use the PC directory itself for simplicity during development
    // In production: use XDG_DATA_HOME or %APPDATA%
    const char* env = getenv("BOOKKEEPING_DATA");
    if (env) return fs::path(env);

    fs::path data_dir = root / "data";
    fs::create_directories(data_dir);
    return data_dir;
}

// ── Main ───────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    int port = 18080;
    if (argc > 1) port = std::stoi(argv[1]);

    std::cout << "══════════════════════════════════════" << std::endl;
    std::cout << "  Bookkeeping Server v1.0" << std::endl;
    std::cout << "══════════════════════════════════════" << std::endl;

    // Locate resources
    fs::path root = findRoot();
    fs::path data_dir = getDataDir(root);

    std::cout << "[Main] Root directory:     " << root << std::endl;
    std::cout << "[Main] Data directory:     " << data_dir << std::endl;
    std::cout << "[Main] Categories JSON:    " << (root / "categories.json") << std::endl;
    std::cout << "[Main] Frontend directory: " << (root / "frontend") << std::endl;

    // Initialize database
    Database db;
    fs::path db_path = data_dir / "bookkeeping.db";
    if (!db.open(db_path.string())) {
        std::cerr << "[Main] FATAL: Cannot open database at " << db_path << std::endl;
        return 1;
    }

    if (!db.initialize((root / "categories.json").string())) {
        std::cerr << "[Main] FATAL: Database initialization failed" << std::endl;
        return 1;
    }

    // Create HTTP server
    httplib::Server svr;

    // ── Serve frontend static files ────────────────────────────
    fs::path frontend_dir = root / "frontend";
    if (!fs::exists(frontend_dir)) {
        std::cerr << "[Main] WARNING: frontend/ directory not found, API only mode" << std::endl;
    } else {
        // Set custom headers for cache control
        svr.set_mount_point("/", frontend_dir.string());
        std::cout << "[Main] Frontend mounted from: " << frontend_dir << std::endl;
    }

    // ── Register API routes ─────────────────────────────────────
    registerRoutes(svr, db);

    // ── Start server ────────────────────────────────────────────
    std::cout << "[Main] Starting HTTP server on http://127.0.0.1:" << port << std::endl;
    std::cout << "[Main] Open http://127.0.0.1:" << port << " in your browser." << std::endl;
    std::cout << "[Main] Press Ctrl+C to stop." << std::endl;
    std::cout << std::endl;

    svr.listen("127.0.0.1", port);

    std::cout << "[Main] Server stopped." << std::endl;
    return 0;
}
