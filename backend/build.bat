@echo off
REM WinDrop Windows Build Script
REM Requires Visual Studio Developer Command Prompt or MinGW-w64

echo ========================================
echo   WinDrop - Windows Build
echo ========================================
echo.

REM Install Node.js dependencies
echo Installing Node.js dependencies...
npm install
if %ERRORLEVEL% NEQ 0 (
    echo Failed to install Node.js dependencies
    exit /b 1
)

echo.
echo Building C++ components...

REM Check for Visual Studio or MinGW
where cl >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    echo Using Visual Studio C++ compiler...
    cl /EHsc /Fe:core.exe platform.cpp core.cpp /link ws2_32.lib iphlpapi.lib
    cl /EHsc /Fe:sender.exe platform.cpp sender.cpp /link ws2_32.lib
) else (
    where g++ >nul 2>&1
    if %ERRORLEVEL% EQU 0 (
        echo Using MinGW-w64 compiler...
        g++ -std=c++11 -pthread platform.cpp core.cpp -o core.exe -lws2_32 -liphlpapi
        g++ -std=c++11 platform.cpp sender.cpp -o sender.exe -lws2_32
    ) else (
        echo ERROR: No C++ compiler found.
        echo Please install Visual Studio or MinGW-w64
        exit /b 1
    )
)

if not exist core.exe (
    echo Failed to compile core.exe
    exit /b 1
)

if not exist sender.exe (
    echo Failed to compile sender.exe
    exit /b 1
)

echo.
echo Build complete!
echo.
echo To run WinDrop:
echo   1. Start core:   core.exe
echo   2. Start server: node index.js
echo   3. Start frontend in another terminal: cd ..\frontend ^&^& npm run dev
echo.