#!/bin/bash

# WinDrop Cross-Platform Build Script
# Supports: macOS, Linux, and provides Windows build instructions

set -e

echo "========================================"
echo "  WinDrop - Cross-Platform File Transfer"
echo "========================================"

# Detect operating system
detect_os() {
    case "$(uname -s)" in
        Darwin*)     echo "macos" ;;
        Linux*)      echo "linux" ;;
        MINGW*|MSYS*|CYGWIN*) echo "windows" ;;
        *)           echo "unknown" ;;
    esac
}

OS=$(detect_os)
echo "Detected OS: $OS"

cd "$(dirname "$0")"

echo ""
echo ">>> Building Backend"
cd backend

# Install Node.js dependencies
if [ ! -d "node_modules" ]; then
    echo "Installing backend dependencies..."
    npm i
fi

# Compile C++ based on OS
case $OS in
    macos)
        echo "Compiling for macOS..."
        g++ -std=c++11 -pthread platform.cpp core.cpp -o core
        g++ -std=c++11 platform.cpp sender.cpp -o sender
        ;;
    linux)
        echo "Compiling for Linux..."
        g++ -std=c++11 -pthread platform.cpp core.cpp -o core
        g++ -std=c++11 platform.cpp sender.cpp -o sender
        ;;
    windows)
        echo "Compiling for Windows..."
        echo "Note: On Windows, use one of these methods:"
        echo "  Method 1: Visual Studio Developer Command Prompt"
        echo "    cl /EHsc /Fe:core.exe platform.cpp core.cpp /link ws2_32.lib iphlpapi.lib"
        echo "    cl /EHsc /Fe:sender.exe platform.cpp sender.cpp /link ws2_32.lib"
        echo ""
        echo "  Method 2: MinGW-w64"
        echo "    g++ -std=c++11 -pthread platform.cpp core.cpp -o core.exe -lws2_32 -liphlpapi"
        echo "    g++ -std=c++11 platform.cpp sender.cpp -o sender.exe -lws2_32"
        echo ""
        echo "For simplicity, this script will try MinGW if available..."
        if command -v g++ &> /dev/null; then
            g++ -std=c++11 -pthread platform.cpp core.cpp -o core.exe -lws2_32 -liphlpapi 2>/dev/null || \
            g++ -std=c++11 -pthread platform.cpp core.cpp -o core.exe 2>/dev/null || \
            echo "Windows compilation requires MSVC or MinGW-w64"
            g++ -std=c++11 platform.cpp sender.cpp -o sender.exe -lws2_32 2>/dev/null || \
            echo "Windows sender compilation requires MSVC or MinGW-w64"
        fi
        ;;
    *)
        echo "Unknown OS. Attempting POSIX compilation..."
        g++ -std=c++11 -pthread platform.cpp core.cpp -o core
        g++ -std=c++11 platform.cpp sender.cpp -o sender
        ;;
esac

if [ -f "core" ] || [ -f "core.exe" ]; then
    echo "✓ Core engine compiled"
else
    echo "✗ Core engine compilation failed"
    exit 1
fi

if [ -f "sender" ] || [ -f "sender.exe" ]; then
    echo "✓ Sender compiled"
else
    echo "✗ Sender compilation failed"
    exit 1
fi

echo ""
echo ">>> Starting Backend Services"

# Start core engine in background
if [ -f "core.exe" ]; then
    ./core.exe &
else
    ./core &
fi
CORE_PID=$!

# Start Node.js server in background
node index.js &
NODE_PID=$!

cd ..

echo "Backend services started (PIDs: $CORE_PID, $NODE_PID)"

echo ""
echo ">>> Building Frontend"
cd frontend

# Install frontend dependencies
if [ ! -d "node_modules" ]; then
    echo "Installing frontend dependencies..."
    npm i
fi

echo ""
echo "========================================"
echo "  Starting WinDrop Application"
echo "========================================"

# Handle Ctrl+C gracefully
trap "echo -e '\n\nShutting down...'; kill -9 $CORE_PID $NODE_PID 2>/dev/null; exit" SIGINT

# Start frontend dev server (no browser)
BROWSER=none npm run dev

# Wait for frontend to exit
wait