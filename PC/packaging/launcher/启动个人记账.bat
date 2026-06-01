@echo off
chcp 65001 >nul
cd /d "%~dp0"

set "PORT=18080"
set "URL=http://127.0.0.1:%PORT%/"

:: 若已在运行则只打开界面
powershell -NoProfile -Command ^
  "try { (Invoke-WebRequest -UseBasicParsing -Uri '%URL%api/health' -TimeoutSec 1).StatusCode } catch { exit 1 }" >nul 2>&1
if %errorlevel% equ 0 goto :open_ui

:: 后台启动服务（窗口最小化）
if exist "bookkeeping-server.exe" (
    start "BookkeepingServer" /min "bookkeeping-server.exe" %PORT%
) else if exist "bookkeeping.exe" (
    start "BookkeepingServer" /min "bookkeeping.exe" %PORT%
) else (
    echo 未找到 bookkeeping-server.exe 或 bookkeeping.exe
    pause
    exit /b 1
)

:: 等待服务就绪（最多约 15 秒）
set /a n=0
:wait_loop
powershell -NoProfile -Command ^
  "try { (Invoke-WebRequest -UseBasicParsing -Uri '%URL%api/health' -TimeoutSec 1).StatusCode } catch { exit 1 }" >nul 2>&1
if %errorlevel% equ 0 goto :open_ui
set /a n+=1
if %n% geq 30 (
    echo 服务启动超时，请检查是否被防火墙拦截端口 %PORT%
    pause
    exit /b 1
)
timeout /t 1 /nobreak >nul
goto :wait_loop

:open_ui
:: 优先 Edge 应用模式（无地址栏）；否则用默认浏览器
where msedge >nul 2>&1
if %errorlevel% equ 0 (
    start "" msedge --app=%URL%
    exit /b 0
)
start "" %URL%
exit /b 0
