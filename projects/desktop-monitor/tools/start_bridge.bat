@echo off
setlocal
chcp 65001 >nul
cd /d "%~dp0"

echo ============================================================
echo  AI Passport Desktop Monitor - Windows Bridge Launcher
echo  AI Passport 电脑监控桥接启动器 (自动串口 + 本机 SSE 流)
echo ============================================================
echo.

set "PYTHON_CMD="
py -3 --version >nul 2>&1
if not errorlevel 1 (
    set "PYTHON_CMD=py -3"
) else (
    python --version >nul 2>&1
    if not errorlevel 1 (
        set "PYTHON_CMD=python"
    )
)

if "%PYTHON_CMD%"=="" (
    echo [ERROR] Python 3 not found. / 未检测到 Python 3。
    echo Please install Python 3.9+ from https://www.python.org/downloads/
    echo 请先安装 Python 3.9+ 并勾选 "Add python.exe to PATH"。
    echo.
    pause
    exit /b 1
)

echo [INFO] Python: %PYTHON_CMD%
%PYTHON_CMD% -c "import psutil, serial" >nul 2>&1
if errorlevel 1 (
    echo [INFO] Installing dependencies from requirements.txt...
    echo [INFO] 检测到缺少 psutil/pyserial，正在自动安装依赖...
    %PYTHON_CMD% -m pip install -r "%~dp0requirements.txt"
    if errorlevel 1 (
        echo.
        echo [ERROR] Failed to install dependencies. / 依赖安装失败。
        echo Manual command / 手动执行:
        echo   %PYTHON_CMD% -m pip install -r "%~dp0requirements.txt"
        echo.
        pause
        exit /b 1
    )
)

if "%~1"=="" (
    set "BRIDGE_ARGS=--port auto"
) else (
    set "BRIDGE_ARGS=%*"
)

echo [INFO] Launching desktop_bridge.py %BRIDGE_ARGS%
echo [INFO] Auto-detects Espressif USB Serial/JTAG (VID 0x303A) with hot-plug reconnect.
echo [INFO] 默认自动识别 Espressif USB Serial/JTAG (VID 0x303A)，支持拔插重连。
echo [INFO] Local SSE stream / 本机模拟流: http://127.0.0.1:8765/stream
echo [INFO] Live Simulator  / 在线体验站: https://1beyond1.github.io/ai-passport-playground/
echo [INFO] Tip / 提示: Pass "--http-only" to run without ESP32 hardware.
echo [INFO] Press Ctrl+C to stop / 按 Ctrl+C 停止。
echo.

%PYTHON_CMD% "%~dp0desktop_bridge.py" %BRIDGE_ARGS%
set "EXIT_CODE=%errorlevel%"
if not "%EXIT_CODE%"=="0" (
    echo.
    echo [WARN] desktop_bridge.py exited with code %EXIT_CODE%.
    pause
)
exit /b %EXIT_CODE%
