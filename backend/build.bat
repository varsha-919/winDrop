@echo off
REM ========================================
REM WinDrop Windows Build Script
REM Supports: Visual Studio or MinGW-w64
REM ========================================

echo ========================================
echo   WinDrop - Windows Build
echo ========================================
echo.

REM Install Node.js dependencies only if needed
if not exist node_modules (
    echo Installing Node.js dependencies...
    call npm install
    if %ERRORLEVEL% NEQ 0 (
        echo Failed to install Node.js dependencies.
        exit /b 1
    )
)

echo.
echo Cleaning old executables...

if exist core.exe del core.exe
if exist sender.exe del sender.exe

echo.
echo Building C++ components...

REM ========================================
REM Visual Studio Compiler
REM ========================================
where cl >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    echo Using Visual Studio C++ compiler...

    cl /EHsc /std:c++17 /Fe:core.exe platform.cpp core.cpp /link ws2_32.lib iphlpapi.lib
    if %ERRORLEVEL% NEQ 0 exit /b 1

    cl /EHsc /std:c++17 /Fe:sender.exe platform.cpp sender.cpp /link ws2_32.lib iphlpapi.lib
    if %ERRORLEVEL% NEQ 0 exit /b 1

) else (

    REM ========================================
    REM MinGW-w64 Compiler
    REM ========================================
    where g++ >nul 2>&1

    if %ERRORLEVEL% EQU 0 (

        echo Using MinGW-w64 compiler...

        g++ -std=c++17 -pthread platform.cpp core.cpp -o core.exe -lws2_32 -liphlpapi
        if %ERRORLEVEL% NEQ 0 exit /b 1

        g++ -std=c++17 platform.cpp sender.cpp -o sender.exe -lws2_32 -liphlpapi
        if %ERRORLEVEL% NEQ 0 exit /b 1

    ) else (

        echo.
        echo ERROR: No C++ compiler found.
        echo Install either:
        echo.
        echo   - Visual Studio Build Tools
        echo   - MSYS2 MinGW-w64
        echo.
        exit /b 1
    )
)

echo.
echo ========================================
echo Build Successful!
echo ========================================
echo.

echo Run WinDrop using:

echo.
echo Terminal 1:
echo   cd backend
echo   node index.js

echo.
echo Terminal 2:
echo   cd frontend
echo   npm run dev

echo.
pause