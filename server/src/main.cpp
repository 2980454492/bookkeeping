// main.cpp — 记账服务进程入口
//
// 启动流程概览：
//   1. 解析监听端口（可选命令行参数）
//   2. findRoot()  定位含 categories.json、frontend/ 的资源目录
//   3. getDataDir() 确定 SQLite 等持久化数据的存放路径
//   4. Database     打开/初始化本地数据库（首次从分类模板灌入默认分类）
//   5. httplib::Server  挂载静态前端 + registerRoutes() 注册 REST API
//   6. listen()     阻塞监听 127.0.0.1，直至 Ctrl+C
//
// 业务逻辑在 db.cpp、handlers.cpp；本文件只做环境与 HTTP 服务装配。

#include "db.h"
#include "handlers.h"

#include <httplib.h>
#include <iostream>
#include <string>
#include <cstdlib>
#include <filesystem>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;

static bool hasAppResources(const fs::path& dir) {
    return fs::exists(dir / "categories.json") && fs::exists(dir / "frontend");
}

static fs::path executableDirectory() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH] = {};
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};
    return fs::path(buf).parent_path();
#else
    char exe_path[4096] = {};
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len <= 0) return {};
    return fs::path(std::string(exe_path, static_cast<size_t>(len))).parent_path();
#endif
}

// ── 查找「资源根目录」────────────────────────────────────────────
// 根目录指同时包含 categories.json（分类模板）和 frontend/（静态页面）的目录。
// main() 据此加载默认分类、挂载 HTTP 静态资源；与 SQLite 数据目录（data/）无关。
//
// 为何需要多种策略：
//   - 工作目录 (cwd) 随启动方式变化：终端里 cd 到哪、快捷方式设定了哪，都不固定。
//   - 可执行文件路径固定：与「从哪条命令启动」无关，适合打包后双击运行。
// CMake POST_BUILD 会把 frontend/、categories.json 复制到可执行文件同目录（见 CMakeLists.txt），
// 因此 build/ 下通常两套路径都能命中；本函数按优先级依次尝试，命中即返回。
static fs::path findRoot() {
    // ── 策略 1：可执行文件所在目录（安装包 / 快捷方式启动时最可靠）──
    fs::path exe_dir = executableDirectory();
    if (!exe_dir.empty() && hasAppResources(exe_dir)) {
        return exe_dir;
    }

    // ── 策略 2：当前工作目录 ─────────────────────────────────────
    fs::path cwd = fs::current_path();
    if (hasAppResources(cwd)) {
        return cwd;
    }

    // ── 策略 3：cwd 在 build/ 时，再试上一级目录 ─────────────────
    // 适用于「在 build 里运行但资源在源码树父级」的旧布局或手工拷贝尚未执行 POST_BUILD 时。
    // 例：cwd=/path/to/bookkeeping/build，则检查 /path/to/bookkeeping/ 是否含资源。
    if (cwd.filename() == "build") {
        fs::path parent = cwd.parent_path();
        if (fs::exists(parent / "categories.json") && fs::exists(parent / "frontend")) {
            return parent;
        }
    }

    // ── 最终兜底：返回 cwd，由后续逻辑打印路径并在缺失 frontend 时降级为仅 API ──
    return cwd;
}

// ── 获取 SQLite 等持久化数据的存放目录 ───────────────────────────
// 与 findRoot() 分离的原因：资源文件（只读、随安装包分发）与用户数据（可写、持续增长）应分开。
//
// 优先级：
//   1. 环境变量 BOOKKEEPING_DATA — 显式指定目录（便于迁移、多用户或遵循 XDG/APPDATA 规范）
//   2. <root>/data/              — 默认：在资源根下创建 data/，存放 bookkeeping.db
static fs::path getDataDir(const fs::path& root) {
    const char* env = getenv("BOOKKEEPING_DATA");
    if (env) {
        // 调用方需保证目录存在且可写；此处不自动创建，避免误写到错误路径
        return fs::path(env);
    }

    fs::path data_dir = root / "data";
    fs::create_directories(data_dir);  // 首次运行自动建目录
    return data_dir;
}

// ── 主函数：装配并运行本地 HTTP 服务 ─────────────────────────────

int main(int argc, char* argv[]) {
    // ── 监听端口 ─────────────────────────────────────────────────
    // 默认 18080，与 run.sh / README 一致；可选：./bookkeeping 8080
    int port = 18080;
    if (argc > 1) {
        try {
            port = std::stoi(argv[1]);
            if (port < 1 || port > 65535) {
                std::cerr << "[Main] 错误: 端口号需在 1-65535 之间，收到: " << argv[1] << std::endl;
                return 1;
            }
        } catch (const std::exception& e) {
            std::cerr << "[Main] 错误: 无法解析端口号 '" << argv[1] << "'，请使用数字端口，如: bookkeeping.exe 18080" << std::endl;
            return 1;
        }
    }

    std::cout << "══════════════════════════════════════" << std::endl;
    std::cout << "  个人记账服务 v1.0" << std::endl;
    std::cout << "══════════════════════════════════════" << std::endl;

    // ── 路径解析 ─────────────────────────────────────────────────
    fs::path root = findRoot();
    fs::path data_dir = getDataDir(root);

    // 启动时打印关键路径，便于排查「找不到前端 / 数据库写哪」类问题
    std::cout << "[Main] 根目录:       " << root << std::endl;
    std::cout << "[Main] 数据目录:     " << data_dir << std::endl;
    std::cout << "[Main] 分类模板:     " << (root / "categories.json") << std::endl;
    std::cout << "[Main] 前端目录:     " << (root / "frontend") << std::endl;

    // ── 数据库 ───────────────────────────────────────────────────
    // open：打开或创建 bookkeeping.db（WAL 等细节见 db.cpp）
    // initialize：建表；若分类表为空则从 categories.json 导入默认二级分类
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

    // ── HTTP 服务 ────────────────────────────────────────────────
    httplib::Server svr;

    // 将 frontend/ 映射到 URL 根路径 "/"：浏览器访问 / 即 index.html，/app.js 等同目录静态文件
    // API 路由由 registerRoutes 注册，路径通常为 /api/...，与静态资源不冲突
    fs::path frontend_dir = root / "frontend";
    if (!fs::exists(frontend_dir)) {
        std::cerr << "[Main] 警告: 未找到 frontend/ 目录，仅提供 API 模式" << std::endl;
    } else {
        svr.set_mount_point("/", frontend_dir.string());
        std::cout << "[Main] 前端已挂载: " << frontend_dir << std::endl;
    }

    // 收支记录、分类、统计、导入导出等 REST 接口（实现见 handlers.cpp）
    const std::string categories_json_path = (root / "categories.json").string();
    registerRoutes(svr, db, categories_json_path, root.string());

    // ── 阻塞监听 ─────────────────────────────────────────────────
    // 仅绑定回环地址，不对外网暴露；适合本机浏览器 + run.sh 自动打开页面的用法
    std::cout << "[Main] HTTP 服务启动: http://127.0.0.1:" << port << std::endl;
    std::cout << "[Main] 请在浏览器中打开 http://127.0.0.1:" << port << std::endl;
    std::cout << "[Main] 按 Ctrl+C 停止服务。" << std::endl;
    std::cout << std::endl;

    svr.listen("127.0.0.1", port);  // 正常退出前不会返回；Ctrl+C 后打印下方日志

    std::cout << "[Main] 服务已停止。" << std::endl;
    return 0;
}
