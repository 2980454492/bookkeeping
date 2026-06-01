# Windows 安装包设计方案

> 目标：生成 **`个人记账 Setup.exe`**，他人双击安装后即可使用，无需安装 Visual Studio、CMake 或手动复制文件。

---

## 1. 交付物定义

| 交付物 | 说明 |
|--------|------|
| **`个人记账-1.0.0-x64-Setup.exe`** | Inno Setup 生成的安装程序（推荐） |
| 安装后目录 | 默认 `%LOCALAPPDATA%\Bookkeeping\`（见 §3） |
| 快捷方式 | 开始菜单 + 可选桌面「个人记账」 |
| 卸载 | 控制面板卸载；**保留用户数据库**（可选策略） |

**不依赖**：开发环境、WSL、浏览器书签（安装包会自带启动方式）。

---

## 2. 总体架构（推荐两进程）

当前后端是 **HTTP 服务 + 静态前端**，安装包阶段建议保持该模型，增加**启动器**：

```
┌─────────────────────────────────────────────────────────┐
│  Bookkeeping.exe（启动器 · 阶段 2 可为 WebView2 壳）      │
│    1. 检测/启动 bookkeeping-server.exe                   │
│    2. 等待 http://127.0.0.1:18080/api/health 就绪        │
│    3. 打开 UI（WebView2 窗口 或 默认浏览器 app 模式）     │
│    4. 退出时结束 server 进程                             │
└─────────────────────────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────┐
│  bookkeeping-server.exe（现有 C++ 后端，可改名）          │
│    frontend/ + categories.json + SQLite + REST API       │
└─────────────────────────────────────────────────────────┘
```

**阶段划分**：

| 阶段 | 启动器 | 用户体验 | 开发量 |
|------|--------|----------|--------|
| **MVP** | `启动个人记账.bat` 或 PowerShell | 自动打开 Edge/Chrome 应用模式 | 小 |
| **正式** | C# + WebView2 小壳（`Bookkeeping.exe`） | 独立窗口，无地址栏 | 中 |
| **进阶** | C++ 内嵌 WebView2，单 exe | 单文件感 | 大 |

安装包 **MVP 即可分发**；WebView2 壳可后续替换 bat，安装路径与文件布局不变。

---

## 3. 安装目录与数据路径（关键）

### 3.1 为何不用 `C:\Program Files\`

当前程序会向**应用根目录**写入：

- `data/bookkeeping.db`
- 导出文件（`POST /api/records/export`）
- 更新后的 `categories.json`

`Program Files` 对普通用户**不可写**，会导致安装后导出/导入/改分类失败。

### 3.2 推荐：每用户本地安装（V1 安装包默认）

| 用途 | 路径 |
|------|------|
| 程序与资源 | `%LOCALAPPDATA%\Bookkeeping\` |
| 数据库 | `%LOCALAPPDATA%\Bookkeeping\data\bookkeeping.db` |
| 导出文件 | `%LOCALAPPDATA%\Bookkeeping\`（与现逻辑一致） |

- **优点**：无需管理员权限、与现有代码兼容（`findRoot` = exe 旁即可）。
- **缺点**：每 Windows 用户一套数据（符合「一设备一套数据」）。

### 3.3 后续（可选）：系统级安装 + 数据分离

安装到 `Program Files`，数据强制 `%APPDATA%\Bookkeeping\` —— 需改 `main.cpp` / `handlers.cpp` 区分「只读资源目录」与「可写数据目录」。列入 V1.1 代码改造，非安装包第一步阻塞项。

### 3.4 Windows 路径代码前置（发布前必须）

`main.cpp` 中 `findRoot()` 在 Windows 上须用 `GetModuleFileNameW` 定位 exe 目录（不能仅用 `/proc/self/exe`）。  
发布 checklist 见 `PC/packaging/RELEASE_CHECKLIST.md`。

---

## 4. 安装包内容清单（Payload）

构建完成后，**`dist/Bookkeeping/`** 目录应包含：

```
Bookkeeping/
├── bookkeeping-server.exe    # 或由 bookkeeping.exe 改名
├── frontend/
│   ├── index.html
│   ├── app.js
│   └── style.css
├── categories.json
├── 启动个人记账.bat          # MVP 启动器（或 Bookkeeping.exe）
└── LICENSE.txt               # 可选
```

**不要**把 `data/`、`.db` 打进安装包（留给首次运行生成）。

---

## 5. 构建流水线

```
开发者机器 (Windows x64)
    │
    ├─① build-release.ps1
    │     cmake Release + 复制资源 → dist/Bookkeeping/
    │
    ├─②（可选）编译 WebView2 启动器 → dist/Bookkeeping/Bookkeeping.exe
    │
    └─③ Inno Setup 编译 bookkeeping.iss
          → 输出: dist/个人记账-1.0.0-x64-Setup.exe
```

### 5.1 环境（一次性）

1. Visual Studio 2022 — 工作负载「使用 C++ 的桌面开发」
2. [vcpkg](https://github.com/microsoft/vcpkg) — `sqlite3:x64-windows`、`zlib:x64-windows`
3. [Inno Setup 6](https://jrsoftware.org/isinfo.php) — 生成 Setup.exe
4. （阶段 2）.NET SDK — WebView2 启动器

### 5.2 命令（在仓库根目录 PowerShell）

```powershell
# ① 构建 payload
.\PC\scripts\build-release.ps1 -VcpkgRoot C:\vcpkg

# ② 制作安装包（需已安装 Inno Setup）
& "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" .\PC\packaging\bookkeeping.iss
```

产物：`dist\个人记账-1.0.0-x64-Setup.exe`

---

## 6. 安装程序行为（Inno Setup）

| 步骤 | 行为 |
|------|------|
| 欢迎页 | 应用名、版本 1.0.0 |
| 目录 | 默认 `{localappdata}\Bookkeeping`，用户可改 |
| 文件 | 从 `dist\Bookkeeping\*` 复制 |
| 快捷方式 | `{userdesktop}` + `{group}` → 启动器 |
| 安装后 | 可选「立即运行」 |
| 卸载 | 删除程序文件；**默认保留** `data\` 与数据库（脚本中可配置） |

### 6.1 WebView2 Runtime

- Win10 21H2+ / Win11 多数已预装。
- 安装包可增加 **[WebView2 Evergreen Bootstrapper](https://developer.microsoft.com/microsoft-edge/webview2/)** 静默安装（仅在使用 WebView2 壳时需要）。
- MVP 仅用系统浏览器时**可不捆绑**。

### 6.2 代码签名（建议）

未签名 exe 会触发 SmartScreen「未知发布者」。对外分发建议：

- 购买代码签名证书，对 `Setup.exe` 与主程序签名；或
- 先小范围分发，告知用户点击「仍要运行」。

---

## 7. 端到端：他人电脑上的体验

1. 收到 `个人记账-1.0.0-x64-Setup.exe`
2. 双击安装 → 选择目录（默认即可）→ 完成
3. 双击桌面「个人记账」
4. 启动器拉起 `bookkeeping-server.exe` → 打开 `http://127.0.0.1:18080`
5. 首次运行自动创建数据库与默认分类
6. 卸载：程序删除；数据目录按卸载脚本策略保留或删除

---

## 8. 版本与升级

| 策略 | 做法 |
|------|------|
| 覆盖安装 | 新版本 Setup 安装到同目录，覆盖 exe 与 `frontend/` |
| 数据保留 | 不覆盖 `data/bookkeeping.db`（Inno 中勿将 db 列入安装文件） |
| 版本号 | `bookkeeping.iss` 的 `AppVersion` 与 CMake `project(VERSION)` 同步 |

---

## 9. 实施路线图

| 序号 | 任务 | 负责 |
|------|------|------|
| 1 | Windows `findRoot()` + 安装到 LocalAppData | C++ |
| 2 | `build-release.ps1` 产出 `dist/Bookkeeping` | 脚本 |
| 3 | MVP 启动 bat + Inno Setup 脚本 | 打包 |
| 4 | 在干净 Win10/11 虚拟机实测安装/卸载/记账/导出 | QA |
| 5 | C# WebView2 启动器替换 bat | 可选 |
| 6 | 代码签名 | 可选 |

---

## 10. 相关文件

| 文件 | 说明 |
|------|------|
| `PC/scripts/build-release.ps1` | Release 构建并组装 dist |
| `PC/packaging/bookkeeping.iss` | Inno Setup 安装脚本 |
| `PC/packaging/launcher/启动个人记账.bat` | MVP 启动器 |
| `PC/packaging/RELEASE_CHECKLIST.md` | 发布前检查项 |
