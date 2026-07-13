# WinDrop - Cross-Platform File Transfer

A fast, peer-to-peer file transfer application that works across Windows, macOS, and Linux.

## Features

- **Cross-Platform**: Transfer files between any combination of Windows, macOS, and Linux machines
- **Peer Discovery**: Automatic network discovery via UDP broadcast
- **Direct Transfer**: TCP-based file transfer between peers
- **Simple UI**: Drag-and-drop interface for easy file sharing
- **Real-time Updates**: Socket.IO for instant peer list updates

## Architecture

```
React Frontend
     ↓
Node.js/Express Backend
     ↓
Socket.IO (Signaling)
     ↓
C++ Networking Engine
     ↓
TCP File Transfer
```

## Project Structure

```
WinDrop/
├── backend/
│   ├── core.cpp         # UDP broadcaster/listener + TCP receiver
│   ├── sender.cpp       # TCP file sender
│   ├── platform.cpp     # Platform abstraction layer
│   ├── platform.h      # Platform abstraction header
│   ├── index.js        # Express + Socket.IO server
│   └── package.json
├── frontend/
│   ├── src/
│   │   ├── App.jsx     # Main React component
│   │   ├── App.css     # Styles
│   │   └── main.jsx    # Entry point
│   └── package.json
├── start.sh            # Build and run script
└── README.md
```

## Platform Abstraction

The C++ networking engine uses a platform abstraction layer (`platform.h`/`platform.cpp`) to handle cross-platform differences:

- **Windows**: WinSock2 (WSAStartup, SOCKET type, closesocket)
- **macOS/Linux**: POSIX sockets (int type, close)
- **Unified API**: `WindropSocket`, `Platform::initialize()`, `SocketUtils::closeSocket()`, etc.

## Building

### macOS / Linux

Run the startup script:

```bash
./start.sh
```

Or manually:

```bash
cd backend
npm install
g++ -std=c++11 -pthread platform.cpp core.cpp -o core
g++ -std=c++11 platform.cpp sender.cpp -o sender
./core &
node index.js &
cd ../frontend
npm install
npm run dev
```

### Windows

#### Method 1: Visual Studio (Developer Command Prompt)

```cmd
cd backend
npm install

cl /EHsc /Fe:core.exe platform.cpp core.cpp /link ws2_32.lib iphlpapi.lib
cl /EHsc /Fe:sender.exe platform.cpp sender.cpp /link ws2_32.lib

core.exe
node index.js
```

#### Method 2: MinGW-w64

```cmd
cd backend
npm install

g++ -std=c++11 -pthread platform.cpp core.cpp -o core.exe -lws2_32 -liphlpapi
g++ -std=c++11 platform.cpp sender.cpp -o sender.exe -lws2_32

core.exe
node index.js
```

## Usage

1. Start the backend services (core engine + Node.js server)
2. Start the frontend dev server
3. Open the web UI (typically http://localhost:5173)
4. Drag and drop a file to send
5. Click a discovered peer to send the file

## Dependencies

### Backend
- Node.js
- Express
- Socket.IO
- Multer (file uploads)

### Frontend
- React 19
- Vite
- Socket.IO Client
- Axios

## Technical Details

### UDP Discovery (Port 8888)
- **Broadcaster**: Sends UDP broadcast every 2 seconds announcing presence
- **Listener**: Receives peer announcements and maintains peer list

### TCP Transfer (Port 8080)
- **Receiver**: Listens for incoming connections, receives filename header, then file data
- **Sender**: Connects to target, sends filename + newline, then file data

### File Transfer Protocol
1. Sender connects to receiver on port 8080
2. Sender sends `<filename>\n` (filename with newline delimiter)
3. Sender streams file data in chunks
4. Receiver saves file to current directory

## Troubleshooting

### No peers discovered
- Ensure all devices are on the same network/subnet
- Check firewall settings (ports 8888, 8080)
- Verify UDP broadcast is enabled on your network

### Connection refused
- Ensure the receiver's core engine is running
- Check the target IP address is correct

### File not received
- Check that the save location has write permissions
- Ensure sufficient disk space

## License

ISC