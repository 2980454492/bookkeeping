@echo off
cd /d "%~dp0"

set "PORT=18080"
set "URL=http://127.0.0.1:%PORT%/"

:: If already running, just open browser
powershell -NoProfile -Command "try { (Invoke-WebRequest -UseBasicParsing -Uri '%URL%api/health' -TimeoutSec 1).StatusCode } catch { exit 1 }" >nul 2>&1
if %errorlevel% equ 0 goto :open_ui

:: Start server in background (minimized)
if exist "bookkeeping-server.exe" (
    start "BookkeepingServer" /min "bookkeeping-server.exe" %PORT%
) else (
    echo [ERROR] bookkeeping-server.exe not found
    pause
    exit /b 1
)

:: Wait for server to be ready (max 15 seconds)
set /a n=0
:wait_loop
powershell -NoProfile -Command "try { (Invoke-WebRequest -UseBasicParsing -Uri '%URL%api/health' -TimeoutSec 1).StatusCode } catch { exit 1 }" >nul 2>&1
if %errorlevel% equ 0 goto :open_ui
set /a n+=1
if %n% geq 30 (
    echo [ERROR] Server startup timeout, check firewall for port %PORT%
    pause
    exit /b 1
)
timeout /t 1 /nobreak >nul
goto :wait_loop

:open_ui
where msedge >nul 2>&1
if %errorlevel% equ 0 (
    start "" msedge --app=%URL%
    exit /b 0
)
start "" %URL%
exit /b 0
