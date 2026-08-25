@echo off
setlocal
title DEEPWORKS MINING - Live Dev Mode

echo ======================================================
echo    DEEPWORKS MINING - LIVE DEVELOPMENT MODE
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

:: 3. Ensure Root Dependencies Exist
if not exist "%~dp0node_modules" (
    echo [*] Installing Root dependencies...
    cd /d "%~dp0"
    call npm install --no-audit --no-fund
)

:: 4. Ensure Backend Dependencies Exist
if not exist "%~dp0backend\node_modules" (
    echo [*] Installing Backend dependencies...
    cd /d "%~dp0backend"
    call npm install --no-audit --no-fund
)

:: 5. Ensure Frontend Dependencies Exist
if not exist "%~dp0frontend\node_modules" (
    echo [*] Installing Frontend dependencies...
    cd /d "%~dp0frontend"
    call npm install --no-audit --no-fund
)

:: 6. Launch Both Simultaneously
echo [*] Starting Backend on Port 3001 and Frontend on Port 5173...
cd /d "%~dp0"
call npm run dev
