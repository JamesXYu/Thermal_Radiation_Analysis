@echo off
REM Thermal Radiation Analysis System - Windows Control Script
REM Usage: run.bat [command]
REM Commands: setup, start, stop, restart, status, test

setlocal EnableDelayedExpansion

set BACKEND_PORT=8080
set FRONTEND_PORT=3000
set BACKEND_BINARY=bin\server.exe

REM ============================================
REM Functions (using GOTO labels)
REM ============================================

if "%1"=="" goto :usage
if /i "%1"=="setup" goto :setup
if /i "%1"=="start" goto :start
if /i "%1"=="persist" goto :persist
if /i "%1"=="stop" goto :stop
if /i "%1"=="restart" goto :restart
if /i "%1"=="status" goto :status
if /i "%1"=="test" goto :test
if /i "%1"=="help" goto :usage
if /i "%1"=="--help" goto :usage
if /i "%1"=="-h" goto :usage

echo [91mERROR: Unknown command: %1[0m
echo.
goto :usage

REM ============================================
REM Setup & Build
REM ============================================
:setup
echo ==========================================
echo Setup ^& Build
echo ==========================================
echo.

REM Check if cpp-httplib exists
if not exist "backend\include\httplib.h" (
    echo Downloading cpp-httplib...
    powershell -Command "Invoke-WebRequest -Uri 'https://raw.githubusercontent.com/yhirose/cpp-httplib/master/httplib.h' -OutFile 'backend\include\httplib.h'"
    if errorlevel 1 (
        echo [91mERROR: Failed to download cpp-httplib[0m
        exit /b 1
    )
    echo [92mSUCCESS: cpp-httplib downloaded[0m
) else (
    echo [92mSUCCESS: cpp-httplib already present[0m
)

echo.
echo Compiling server...
if not exist "bin" mkdir bin

REM Try g++ first (MinGW)
where g++ >nul 2>&1
if %errorlevel%==0 (
    g++ -std=c++17 -O2 -o %BACKEND_BINARY% backend\server.cpp -I backend -lpthread
    if errorlevel 1 (
        echo [91mERROR: Failed to compile server with g++[0m
        echo.
        echo Make sure MinGW or MSYS2 is installed and in PATH
        echo Download from: https://www.msys2.org/
        exit /b 1
    )
    echo [92mSUCCESS: Server compiled successfully[0m
    echo.
    echo [92mSetup complete! Run 'run.bat start' to begin[0m
    goto :eof
) else (
    echo [91mERROR: g++ compiler not found[0m
    echo.
    echo Please install MinGW or MSYS2:
    echo   1. Download MSYS2: https://www.msys2.org/
    echo   2. Install and run: pacman -S mingw-w64-x86_64-gcc
    echo   3. Add to PATH: C:\msys64\mingw64\bin
    exit /b 1
)

REM ============================================
REM Check if port is in use
REM ============================================
:is_port_used
set PORT=%1
netstat -ano | findstr ":%PORT% " | findstr "LISTENING" >nul 2>&1
exit /b

REM ============================================
REM Get PID of process on port
REM ============================================
:get_pid_on_port
set PORT=%1
for /f "tokens=5" %%a in ('netstat -ano ^| findstr ":%PORT% " ^| findstr "LISTENING"') do (
    set PID=%%a
    goto :pid_found
)
:pid_found
exit /b

REM ============================================
REM Start Backend
REM ============================================
:start_backend
if not exist "%BACKEND_BINARY%" (
    echo [91mERROR: Backend not compiled. Run 'run.bat setup' first[0m
    exit /b 1
)

call :is_port_used %BACKEND_PORT%
if %errorlevel%==0 (
    echo [92mSUCCESS: Backend already running on port %BACKEND_PORT%[0m
    exit /b 0
)

echo Starting backend server...
start /B "" "%BACKEND_BINARY%"
timeout /t 2 /nobreak >nul

call :is_port_used %BACKEND_PORT%
if %errorlevel%==0 (
    echo [92mSUCCESS: Backend started on http://localhost:%BACKEND_PORT%[0m
    exit /b 0
) else (
    echo [91mERROR: Failed to start backend[0m
    exit /b 1
)

REM ============================================
REM Start Frontend
REM ============================================
:start_frontend
call :is_port_used %FRONTEND_PORT%
if %errorlevel%==0 (
    echo [92mSUCCESS: Frontend already running on port %FRONTEND_PORT%[0m
    exit /b 0
)

echo Starting frontend server...

REM Check if python3 exists, otherwise try python
where python3 >nul 2>&1
if %errorlevel%==0 (
    start /B "" python3 -m http.server %FRONTEND_PORT% --bind 0.0.0.0 --directory frontend
) else (
    where python >nul 2>&1
    if %errorlevel%==0 (
        start /B "" python -m http.server %FRONTEND_PORT% --bind 0.0.0.0 --directory frontend
    ) else (
        echo [91mERROR: Python not found. Please install Python 3[0m
        exit /b 1
    )
)

timeout /t 2 /nobreak >nul

call :is_port_used %FRONTEND_PORT%
if %errorlevel%==0 (
    echo [92mSUCCESS: Frontend started on http://localhost:%FRONTEND_PORT%[0m
    exit /b 0
) else (
    echo [91mERROR: Failed to start frontend[0m
    exit /b 1
)

REM ============================================
REM Start System
REM ============================================
:start
echo ==========================================
echo Starting System
echo ==========================================
echo.

call :start_backend
set BACKEND_OK=%errorlevel%

call :start_frontend
set FRONTEND_OK=%errorlevel%

if %BACKEND_OK%==0 if %FRONTEND_OK%==0 (
    echo.
    echo ==========================================
    echo [92mSystem Ready![0m
    echo ==========================================
    echo.
    echo Open your browser:
    echo   [92mhttp://localhost:%FRONTEND_PORT%[0m
    echo.
    echo Backend API:
    echo   http://localhost:%BACKEND_PORT%
    echo.
    echo To stop: run.bat stop
    echo.
    
    REM Auto-open browser
    timeout /t 1 /nobreak >nul
    start http://localhost:%FRONTEND_PORT%
)
goto :eof

REM ============================================
REM Start System in Persistent Mode
REM ============================================
:persist
echo ==========================================
echo Starting System in Persistent Mode
echo ==========================================
echo.
echo This will keep servers running even after you disconnect.
echo Servers will restart automatically if they crash.
echo.

REM Create logs directory
if not exist "logs" mkdir logs

REM Create persistent backend wrapper
echo @echo off > _backend_persist.bat
echo :loop >> _backend_persist.bat
echo cd /d "%~dp0" >> _backend_persist.bat
echo echo [Backend] Starting at %%date%% %%time%% ^>^> logs\backend.log >> _backend_persist.bat
echo "%BACKEND_BINARY%" ^>^> logs\backend.log 2^>^&1 >> _backend_persist.bat
echo echo [Backend] Crashed at %%date%% %%time%%, restarting in 5 seconds... ^>^> logs\backend.log >> _backend_persist.bat
echo timeout /t 5 /nobreak ^>nul >> _backend_persist.bat
echo goto loop >> _backend_persist.bat

REM Create persistent frontend wrapper
echo @echo off > _frontend_persist.bat
echo :loop >> _frontend_persist.bat
echo cd /d "%~dp0" >> _frontend_persist.bat
echo echo [Frontend] Starting at %%date%% %%time%% ^>^> logs\frontend.log >> _frontend_persist.bat
where python3 >nul 2>&1
if %errorlevel%==0 (
    echo python3 -m http.server %FRONTEND_PORT% --bind 0.0.0.0 --directory frontend ^>^> logs\frontend.log 2^>^&1 >> _frontend_persist.bat
) else (
    echo python -m http.server %FRONTEND_PORT% --bind 0.0.0.0 --directory frontend ^>^> logs\frontend.log 2^>^&1 >> _frontend_persist.bat
)
echo echo [Frontend] Crashed at %%date%% %%time%%, restarting in 5 seconds... ^>^> logs\frontend.log >> _frontend_persist.bat
echo timeout /t 5 /nobreak ^>nul >> _frontend_persist.bat
echo goto loop >> _frontend_persist.bat

REM Check if already running
call :is_port_used %BACKEND_PORT%
if %errorlevel%==0 (
    echo [92mBackend already running on port %BACKEND_PORT%[0m
) else (
    echo Starting persistent backend...
    start "TRA Backend" /MIN cmd /c _backend_persist.bat
    timeout /t 2 /nobreak >nul
    call :is_port_used %BACKEND_PORT%
    if %errorlevel%==0 (
        echo [92mSUCCESS: Backend started[0m
    ) else (
        echo [91mERROR: Backend failed to start. Check logs\backend.log[0m
    )
)

call :is_port_used %FRONTEND_PORT%
if %errorlevel%==0 (
    echo [92mFrontend already running on port %FRONTEND_PORT%[0m
) else (
    echo Starting persistent frontend...
    start "TRA Frontend" /MIN cmd /c _frontend_persist.bat
    timeout /t 2 /nobreak >nul
    call :is_port_used %FRONTEND_PORT%
    if %errorlevel%==0 (
        echo [92mSUCCESS: Frontend started[0m
    ) else (
        echo [91mERROR: Frontend failed to start. Check logs\frontend.log[0m
    )
)

echo.
echo ==========================================
echo [92mSystem Running in Persistent Mode![0m
echo ==========================================
echo.
echo The servers will keep running even after you disconnect.
echo.
echo Access at:
echo   [92mhttp://localhost:%FRONTEND_PORT%[0m
echo   [92mhttp://10.11.2.164:%FRONTEND_PORT%[0m
echo.
echo Backend API:
echo   http://localhost:%BACKEND_PORT%
echo   http://10.11.2.164:%BACKEND_PORT%
echo.
echo Logs are being written to:
echo   logs\backend.log
echo   logs\frontend.log
echo.
echo To check status: run.bat status
echo To stop servers: run.bat stop
echo.
echo You can now safely disconnect from the server.
echo.
goto :eof

REM ============================================
REM Stop Servers
REM ============================================
:stop
echo ==========================================
echo Stopping System
echo ==========================================
echo.

REM Stop backend
call :get_pid_on_port %BACKEND_PORT%
if defined PID (
    taskkill /F /PID !PID! >nul 2>&1
    echo [92mSUCCESS: Backend stopped[0m
) else (
    echo [93mINFO: Backend not running[0m
)

REM Stop frontend
call :get_pid_on_port %FRONTEND_PORT%
if defined PID (
    taskkill /F /PID !PID! >nul 2>&1
    echo [92mSUCCESS: Frontend stopped[0m
) else (
    echo [93mINFO: Frontend not running[0m
)

REM Stop persistent wrapper processes (if running)
tasklist /FI "WINDOWTITLE eq TRA Backend" 2>nul | find "cmd.exe" >nul
if %errorlevel%==0 (
    taskkill /F /FI "WINDOWTITLE eq TRA Backend" >nul 2>&1
    echo [92mSUCCESS: Backend wrapper stopped[0m
)

tasklist /FI "WINDOWTITLE eq TRA Frontend" 2>nul | find "cmd.exe" >nul
if %errorlevel%==0 (
    taskkill /F /FI "WINDOWTITLE eq TRA Frontend" >nul 2>&1
    echo [92mSUCCESS: Frontend wrapper stopped[0m
)

echo.
echo [92mAll servers stopped[0m
goto :eof

REM ============================================
REM Restart
REM ============================================
:restart
echo ==========================================
echo Restarting System
echo ==========================================
call :stop
timeout /t 2 /nobreak >nul
call :start
goto :eof

REM ============================================
REM Status
REM ============================================
:status
echo ==========================================
echo System Status
echo ==========================================
echo.

echo Backend (port %BACKEND_PORT%):
call :is_port_used %BACKEND_PORT%
if %errorlevel%==0 (
    call :get_pid_on_port %BACKEND_PORT%
    echo [92mRunning (PID: !PID!)[0m
    
    REM Test backend
    powershell -Command "(Invoke-WebRequest -Uri 'http://localhost:%BACKEND_PORT%/health' -UseBasicParsing).Content" 2>nul
) else (
    echo [91mNot running[0m
)

echo.
echo Frontend (port %FRONTEND_PORT%):
call :is_port_used %FRONTEND_PORT%
if %errorlevel%==0 (
    call :get_pid_on_port %FRONTEND_PORT%
    echo [92mRunning (PID: !PID!)[0m
    echo   URL: http://localhost:%FRONTEND_PORT%
) else (
    echo [91mNot running[0m
)
echo.
goto :eof

REM ============================================
REM Test System
REM ============================================
:test
echo ==========================================
echo Testing System
echo ==========================================
echo.

echo Testing backend health...
powershell -Command "try { $response = Invoke-WebRequest -Uri 'http://localhost:%BACKEND_PORT%/health' -UseBasicParsing; Write-Host '[92mSUCCESS: Backend responding: ' $response.Content '[0m' } catch { Write-Host '[91mERROR: Backend not responding[0m' }"

echo.
echo Testing backend status...
powershell -Command "try { $response = Invoke-WebRequest -Uri 'http://localhost:%BACKEND_PORT%/status' -UseBasicParsing; Write-Host '[92mSUCCESS: Status: ' $response.Content '[0m' } catch { Write-Host '[91mERROR: Status check failed[0m' }"

echo.
echo Testing frontend...
powershell -Command "try { $response = Invoke-WebRequest -Uri 'http://localhost:%FRONTEND_PORT%' -UseBasicParsing; Write-Host '[92mSUCCESS: Frontend responding (HTTP ' $response.StatusCode ')[0m' } catch { Write-Host '[91mERROR: Frontend not responding[0m' }"

echo.
goto :eof

REM ============================================
REM Usage
REM ============================================
:usage
echo Thermal Radiation Analysis System - Windows Control Script
echo.
echo Usage: run.bat [command]
echo.
echo Commands:
echo   setup      Download dependencies and compile
echo   start      Start both servers and open browser (session-dependent)
echo   persist    Start servers in persistent mode (survives disconnects)
echo   stop       Stop all servers
echo   restart    Restart all servers
echo   status     Check if servers are running
echo   test       Test server endpoints
echo   help       Show this help message
echo.
echo Examples:
echo   run.bat setup      # First time setup
echo   run.bat persist    # Start in persistent mode (RECOMMENDED for servers)
echo   run.bat start      # Start for local testing
echo   run.bat status     # Check what's running
echo   run.bat stop       # Stop everything
echo.
echo For remote/company servers, use 'persist' to keep running after disconnect.
echo.
goto :eof

