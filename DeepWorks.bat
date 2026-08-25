@echo off
setlocal
title DEEPWORKS MINING - Subsurface Telemetry and Safety Gateway

echo ======================================================
echo    DEEPWORKS MINING - MYOSA HARDWARE CENTRAL GATEWAY
echo ======================================================
echo.

:: 1. Verify Node.js is installed
where node >nul 2>nul
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Node.js is not found in PATH! Please install Node.js v18+.
    pause
    exit /b 1
)

:: 2. Terminate previous backend gateway instances on port 3001
echo [*] Checking and freeing port 3001 if occupied...
powershell -NoProfile -Command "$conns = Get-NetTCPConnection -LocalPort 3001 -ErrorAction SilentlyContinue; if ($conns) { $conns | ForEach-Object { Stop-Process -Id $_.OwningProcess -Force -ErrorAction SilentlyContinue } }; Get-CimInstance Win32_Process -Filter \"Name = 'node.exe'\" -ErrorAction SilentlyContinue | Where-Object { $_.CommandLine -like '*server.js*' } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }" >nul 2>nul

:: 3. Ensure Backend Dependencies Exist
if not exist "%~dp0backend\node_modules" (
    echo [*] Installing Backend dependencies...
    cd /d "%~dp0backend"
    call npm install --no-audit --no-fund
)

:: 4. Ensure Frontend Dependencies and Production Build Exist
if not exist "%~dp0frontend\node_modules" (
    echo [*] Installing Frontend dependencies...
    cd /d "%~dp0frontend"
    call npm install --no-audit --no-fund
)

if not exist "%~dp0frontend\dist\index.html" (
    echo [*] Building Frontend production bundle into dist...
    cd /d "%~dp0frontend"
    call npm run build
)

:: 5. Open Browser once Server is ready
echo [*] Scheduling dashboard browser launch...
start "" powershell -NoProfile -WindowStyle Hidden -Command "for ($i=0; $i -lt 30; $i++) { Start-Sleep -Milliseconds 500; try { $r = Invoke-WebRequest -Uri 'http://localhost:3001' -UseBasicParsing -TimeoutSec 1; if ($r.StatusCode -eq 200) { if (Get-Command msedge -ErrorAction SilentlyContinue) { Start-Process msedge -ArgumentList '--app=http://localhost:3001','--window-size=1440,920' } elseif (Get-Command chrome -ErrorAction SilentlyContinue) { Start-Process chrome -ArgumentList '--app=http://localhost:3001','--window-size=1440,920' } else { Start-Process 'http://localhost:3001' }; break } } catch {} }"

:: 6. Start Backend Server (Foreground live gateway)
echo [*] Starting Central Hardware Gateway Server on COM7...
echo.
cd /d "%~dp0backend"
node server.js
