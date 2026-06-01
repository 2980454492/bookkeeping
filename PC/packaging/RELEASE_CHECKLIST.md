# Windows 安装包发布检查清单

## 构建环境

- [ ] 在 **Windows x64** 本机构建（非 WSL 的 Linux 二进制）
- [ ] Release 配置，`bookkeeping.exe` 可独立运行
- [ ] `frontend/`、`categories.json` 与 exe 同目录

## 路径与权限

- [ ] Windows 下 `findRoot()` 能通过 exe 目录找到资源（见 `main.cpp`）
- [ ] 安装目标为 `%LOCALAPPDATA%\Bookkeeping`（或可写目录）
- [ ] 导出、导入、修改分类后 `categories.json` 可写

## 安装包

- [ ] `build-release.ps1` 成功生成 `dist/Bookkeeping/`
- [ ] Inno Setup 生成 `个人记账-*-Setup.exe`
- [ ] 安装后快捷方式可启动
- [ ] 卸载后程序文件已删除；数据库策略符合预期（保留/删除）

## 干净机器测试（虚拟机推荐）

- [ ] Win10 或 Win11，**未安装** VS / CMake
- [ ] 安装 → 新增记录 → 筛选 → 导出 CSV → 导入 → 关闭再打开数据仍在
- [ ] 防火墙未拦截 127.0.0.1:18080

## 分发

- [ ] 安装包文件名含版本号
- [ ] （可选）代码签名
- [ ] 附带简短「系统要求」：Windows 10+ x64，约 XX MB 磁盘
