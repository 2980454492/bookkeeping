# 💰 个人记账 (Bookkeeping)

轻量级个人记账应用，支持 **PC 桌面端 (.exe)、Web 网页版、微信小程序、Android APP** 四大平台。

> 当前仓库：**PC 桌面端**（C++ 后端 + Web 前端）

---

## 功能概览

- 📝 收支记录 CRUD（日期精确到分钟）
- 📂 二级分类体系（8 个支出一级 + 5 个收入一级，支持自定义）
- 🔍 筛选排序（按日期/金额、分类、类型）
- 📊 柱状图 / 饼图 / 折线图（按天/周/月/年统计）
- 📥 导入导出 JSON / CSV / Excel
- 💾 SQLite 本地存储，完全离线
- 🚫 无需登录，数据完全私有

> 详细需求文档：[requiredment.md](requiredment.md)

---

## 项目结构

```
bookkeeping/
├── build/                # CMake 构建输出
├── PC/                   # PC 端源码
│   ├── run.sh            # 一键运行脚本
│   ├── CMakeLists.txt    # CMake 构建配置
│   ├── src/              # C++ 源码
│   │   ├── main.cpp      #   入口：HTTP 服务 + 静态文件
│   │   ├── db.h/cpp      #   SQLite 数据库封装
│   │   └── handlers.h/cpp#   REST API 处理器
│   ├── libs/             # 第三方头文件
│   │   ├── httplib.h     #   cpp-httplib (HTTP 服务)
│   │   └── json.hpp      #   nlohmann/json (JSON 解析)
│   ├── frontend/         # Web 前端
│   │   ├── index.html
│   │   ├── style.css
│   │   └── app.js
│   └── categories.json   # 默认分类模板
└── requiredment.md       # 需求文档
```

---

## 环境要求

### Linux (含 WSL)

| 依赖 | 版本要求 | 安装命令 |
|------|---------|---------|
| GCC (g++) | ≥ 9.0 | `sudo apt install build-essential` |
| CMake | ≥ 3.16 | `sudo apt install cmake` |
| SQLite3 开发库 | ≥ 3.30 | `sudo apt install libsqlite3-dev` |

```bash
# 一键安装所有依赖 (Ubuntu/Debian)
sudo apt update && sudo apt install -y build-essential cmake libsqlite3-dev
```

### Windows

| 依赖 | 说明 |
|------|------|
| Visual Studio 2022 | 需勾选"使用 C++ 的桌面开发"工作负载 |
| CMake | VS 自带或在 [cmake.org](https://cmake.org) 下载 |
| Windows SDK | VS 自带（含 WebView2） |

> 注：Windows 上编译可启用 WebView2，生成原生桌面窗口而非浏览器模式。

---

## 快速开始

### Linux / WSL

```bash
# 1. 克隆项目
git clone https://github.com/2980454492/bookkeeping.git && cd bookkeeping

# 2. 下载第三方头文件依赖（仅首次需要）
mkdir -p PC/libs
curl -L "https://cdn.jsdelivr.net/gh/yhirose/cpp-httplib@v0.16.3/httplib.h" -o PC/libs/httplib.h
curl -L "https://cdn.jsdelivr.net/gh/nlohmann/json@v3.11.3/single_include/nlohmann/json.hpp" -o PC/libs/json.hpp

# 3. 一键运行（构建 + 启动 + 打开浏览器）
cd PC 
chmod +x run.sh
./run.sh
```

浏览器自动打开 `http://127.0.0.1:18080`，即可看到记账界面。

### 手动构建运行

```bash
# 构建
mkdir -p build && cd build
cmake ../PC -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)

# 运行
./bookkeeping
# 浏览器打开 http://127.0.0.1:18080
```

### Windows

```powershell
# 1. 下载头文件依赖（同上）
# 2. CMake 构建
mkdir build && cd build
cmake ..\PC -G "Visual Studio 17 2022"
cmake --build . --config Release

# 3. 运行
.\Release\bookkeeping.exe
```

---

## API 接口

服务启动后，所有 API 位于 `http://127.0.0.1:18080/api/`：

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/health` | 健康检查 |
| GET | `/api/records` | 查询记录（支持筛选/分页/排序） |
| POST | `/api/records` | 新增记录 |
| PUT | `/api/records/:id` | 修改记录 |
| DELETE | `/api/records/:id` | 删除记录 |
| GET | `/api/categories` | 获取分类列表 |
| POST | `/api/categories/l1` | 新增一级分类 |
| DELETE | `/api/categories/l1/:id` | 删除一级分类 |
| POST | `/api/categories/reset` | 恢复默认分类 |

---

## 许可

MIT License
