@echo off
setlocal
cd /d "%~dp0"

if "%COMMONLIBF4_XMAKE_ROOT%"=="" (
    echo [ERROR] Set COMMONLIBF4_XMAKE_ROOT first.
    echo         It must point to the CommonLibF4 xmake root.
    exit /b 2
)

where xmake >nul 2>&1
if errorlevel 1 (
    echo [ERROR] xmake was not found in PATH.
    exit /b 2
)

where cl >nul 2>&1
if errorlevel 1 (
    echo [ERROR] MSVC cl.exe was not found.
    echo         Run this script from a Visual Studio developer command prompt.
    exit /b 2
)

echo [1/2] Configuring xmake with MSVC...
xmake f -c --toolchain=msvc
if errorlevel 1 (
    echo [ERROR] xmake configuration failed.
    exit /b 1
)

echo [2/2] Building PowerArmorMgr releasedbg...
xmake build -y -m releasedbg -j2
if errorlevel 1 (
    echo [ERROR] Build failed.
    exit /b 1
)

echo [DONE] Native DLL build completed.
echo        No live Fallout 4 or MO2 directory was modified.
exit /b 0
