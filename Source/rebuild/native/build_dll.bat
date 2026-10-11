@echo off
setlocal
cd /d "%~dp0"
if "%COMMONLIBF4_XMAKE_ROOT%"=="" (
    echo [ERROR] Set COMMONLIBF4_XMAKE_ROOT first.
    exit /b 2
)
where xmake >nul 2>&1
if errorlevel 1 exit /b 2
where cl >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Open an x64 Visual Studio developer command prompt.
    exit /b 2
)
xmake f -c --toolchain=msvc
if errorlevel 1 exit /b 1
xmake build -y -m releasedbg -j2
if errorlevel 1 exit /b 1
echo [OK] Experimental v3 core DLL built. Not packaged or installed.
exit /b 0
