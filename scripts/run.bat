@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"

set "PORT=18080"
set "URL=http://127.0.0.1:%PORT%/"

:: ── 检查 PowerShell 是否可用 ──────────────────────────────────
where powershell >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] PowerShell not found. Windows 7+ required.
    pause
    exit /b 1
)

:: ── 如果服务已在运行，直接打开浏览器 ─────────────────────────
powershell -NoProfile -Command "try { (Invoke-WebRequest -UseBasicParsing -Uri '%URL%api/health' -TimeoutSec 2).StatusCode } catch { exit 1 }" >nul 2>&1
if %errorlevel% equ 0 goto :open_ui

:: ── 查找可执行文件 ────────────────────────────────────────────
set "EXE="
if exist "bookkeeping-server.exe" (
    set "EXE=bookkeeping-server.exe"
) else if exist "bookkeeping.exe" (
    set "EXE=bookkeeping.exe"
) else (
    echo [ERROR] bookkeeping-server.exe not found in %cd%
    echo Please ensure the exe is in the same directory as this script.
    pause
    exit /b 1
)

:: ── 后台启动服务（窗口最小化） ─────────────────────────────────
echo Starting server on port %PORT%...
start "BookkeepingServer" /min "!EXE!" %PORT%

:: ── 等待服务就绪（最多 30 秒） ──────────────────────────────────
set /a n=0
:wait_loop
powershell -NoProfile -Command "try { (Invoke-WebRequest -UseBasicParsing -Uri '%URL%api/health' -TimeoutSec 1).StatusCode } catch { exit 1 }" >nul 2>&1
if %errorlevel% equ 0 goto :open_ui
set /a n+=1
if %n% geq 30 (
    echo [ERROR] Server startup timeout after 30s
    echo        Check if port %PORT% is blocked by firewall.
    echo        Try running !EXE! directly to see error messages.
    pause
    exit /b 1
)
timeout /t 1 /nobreak >nul
goto :wait_loop

:: ── 打开浏览器 ─────────────────────────────────────────────────
:open_ui
where msedge >nul 2>&1
if %errorlevel% equ 0 (
    start "" msedge --app=%URL%
    exit /b 0
)
start "" %URL%
exit /b 0
