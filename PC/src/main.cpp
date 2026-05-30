#include "db.h"
#include "handlers.h"

#include <httplib.h>
#include <iostream>
#include <string>
#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;

// ── 查找项目根目录（categories.json 和 frontend/ 所在位置） ──────
// 开发阶段：优先从当前工作目录查找
// 生产环境：从可执行文件所在目录查找
static fs::path findRoot() {
    // 1. 尝试当前工作目录（适用于 `cd build && ./bookkeeping`）
    fs::path cwd = fs::current_path();
    if (fs::exists(cwd / "categories.json") && fs::exists(cwd / "frontend")) {
        return cwd;
    }

    // 2. 尝试可执行文件所在目录
    char exe_path[4096] = {};
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len > 0) {
        fs::path exe_dir = fs::path(std::string(exe_path, len)).parent_path();
        if (fs::exists(exe_dir / "categories.json") && fs::exists(exe_dir / "frontend")) {
            return exe_dir;
        }
    }

    // 3. 兜底方案：build 目录的父目录（适用于 CMake 源码外构建）
    if (cwd.filename() == "build") {
        fs::path parent = cwd.parent_path();
        if (fs::exists(parent / "categories.json") && fs::exists(parent / "frontend")) {
            return parent;
        }
    }

    return cwd; // 最终兜底
}

// ── 获取 SQLite 数据库存放目录 ─────────────────────────────────────
static fs::path getDataDir(const fs::path& root) {
    // 开发阶段直接使用当前目录下的 data/ 子目录
    // 生产环境可使用环境变量 BOOKKEEPING_DATA 指定（如 XDG_DATA_HOME 或 %APPDATA%）
    const char* env = getenv("BOOKKEEPING_DATA");
    if (env) return fs::path(env);

    fs::path data_dir = root / "data";
    fs::create_directories(data_dir);
    return data_dir;
}

// ── 主函数 ───────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    int port = 18080;
    if (argc > 1) port = std::stoi(argv[1]);

    std::cout << "══════════════════════════════════════" << std::endl;
    std::cout << "  个人记账服务 v1.0" << std::endl;
    std::cout << "══════════════════════════════════════" << std::endl;

    // 定位资源文件
    fs::path root = findRoot();
    fs::path data_dir = getDataDir(root);

    std::cout << "[Main] 根目录:       " << root << std::endl;
    std::cout << "[Main] 数据目录:     " << data_dir << std::endl;
    std::cout << "[Main] 分类模板:     " << (root / "categories.json") << std::endl;
    std::cout << "[Main] 前端目录:     " << (root / "frontend") << std::endl;

    // 初始化数据库
    Database db;
    fs::path db_path = data_dir / "bookkeeping.db";
    if (!db.open(db_path.string())) {
        std::cerr << "[Main] 致命错误: 无法打开数据库 " << db_path << std::endl;
        return 1;
    }

    if (!db.initialize((root / "categories.json").string())) {
        std::cerr << "[Main] 致命错误: 数据库初始化失败" << std::endl;
        return 1;
    }

    // 创建 HTTP 服务器
    httplib::Server svr;

    // ── 挂载前端静态文件 ────────────────────────────────────────
    fs::path frontend_dir = root / "frontend";
    if (!fs::exists(frontend_dir)) {
        std::cerr << "[Main] 警告: 未找到 frontend/ 目录，仅提供 API 模式" << std::endl;
    } else {
        // 设置自定义缓存头
        svr.set_mount_point("/", frontend_dir.string());
        std::cout << "[Main] 前端已挂载: " << frontend_dir << std::endl;
    }

    // ── 注册 API 路由 ────────────────────────────────────────────
    registerRoutes(svr, db);

    // ── 启动服务器 ───────────────────────────────────────────────
    std::cout << "[Main] HTTP 服务启动: http://127.0.0.1:" << port << std::endl;
    std::cout << "[Main] 请在浏览器中打开 http://127.0.0.1:" << port << std::endl;
    std::cout << "[Main] 按 Ctrl+C 停止服务。" << std::endl;
    std::cout << std::endl;

    svr.listen("127.0.0.1", port);

    std::cout << "[Main] 服务已停止。" << std::endl;
    return 0;
}
