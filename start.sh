#!/bin/bash

set -e

echo "========================================"
echo "  WinDrop - Cross Platform File Transfer"
echo "========================================"

detect_os() {
    case "$(uname -s)" in
        Darwin*) echo "macos" ;;
        Linux*) echo "linux" ;;
        MINGW*|MSYS*|CYGWIN*) echo "windows" ;;
        *) echo "unknown" ;;
    esac
}

OS=$(detect_os)

echo "Detected OS: $OS"

cd "$(dirname "$0")"

echo ""
echo ">>> Building Backend"


cd backend

if [ ! -d node_modules ]; then
    echo "Installing backend dependencies..."
    npm install
fi

case $OS in

macos)

    echo "Compiling for macOS..."

    g++ -std=c++17 -pthread platform.cpp core.cpp -o core
    g++ -std=c++17 platform.cpp sender.cpp -o sender
    ;;

linux)

    echo "Compiling for Linux..."

    g++ -std=c++17 -pthread platform.cpp core.cpp -o core
    g++ -std=c++17 platform.cpp sender.cpp -o sender
    ;;

windows)

    echo "Compiling for Windows..."

    if command -v g++ >/dev/null 2>&1; then

        g++ -std=c++17 -pthread platform.cpp core.cpp -o core.exe -lws2_32 -liphlpapi
        g++ -std=c++17 platform.cpp sender.cpp -o sender.exe -lws2_32 -liphlpapi

    else

        echo "No MinGW compiler found."
        exit 1

    fi
    ;;

*)

    echo "Unknown operating system."
    exit 1
    ;;

esac

echo "✓ Backend build successful"

echo ""
echo ">>> Starting Backend"

node index.js &
NODE_PID=$!

cd ..

echo ""
echo ">>> Building Frontend"

cd frontend

if [ ! -d node_modules ]; then
    echo "Installing frontend dependencies..."
    npm install
fi

echo ""
echo "========================================"
echo "Starting WinDrop"
echo "========================================"

trap 'echo ""; echo "Stopping..."; kill $NODE_PID 2>/dev/null; exit' SIGINT

BROWSER=none npm run dev

wait