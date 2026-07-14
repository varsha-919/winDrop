const express = require("express");
const http = require("http");
const { Server } = require("socket.io");
const cors = require("cors");
const { spawn } = require("child_process");
const multer = require("multer");
const fs = require("fs");

const app = express();
const os = require("os");

// Helper to get all IP addresses of the CURRENT machine
function getMyIPs() {
  const ips = [];
  const interfaces = os.networkInterfaces();
  for (const name of Object.keys(interfaces)) {
    for (const iface of interfaces[name]) {
      if (iface.family === "IPv4" && !iface.internal) {
        ips.push(iface.address);
      }
    }
  }
  return ips;
}
const myIps = getMyIPs();
app.use(cors());
app.set('trust proxy', 1); // Enable to get correct client IP behind proxies
app.use(express.json());

// Return this machine's LAN IP
app.get("/my-ip", (req, res) => {
  const ips = getMyIPs();

  console.log("🌐 Returning LAN IP:", ips[0]);

  res.json({
    ip: ips[0] || "127.0.0.1"
  });
});

const server = http.createServer(app);
const io = new Server(server, { cors: { origin: "*" } });

// 🔥 STORE UNIQUE PEERS
const peers = new Map();

// 🔥 STORE PENDING TRANSFER REQUESTS
const pendingRequests = new Map();
const requestTimeouts = new Map();

// 🔥 STORE UPLOADED FILE PATHS (linked to targetIp)
const uploadedFiles = new Map(); // targetIp -> { filename, filePath, fileSize, uploadedAt }

// 🔥 TRANSFER STATUS TRACKING
const activeTransfers = new Map(); // requestId -> { status, progress, ... }

// --- MULTER CONFIG ---
const storage = multer.diskStorage({
  destination: (req, file, cb) => {
    if (!fs.existsSync("./uploads")) fs.mkdirSync("./uploads");
    cb(null, "./uploads");
  },
  filename: (req, file, cb) => cb(null, file.originalname),
});
const upload = multer({ storage: storage });

const path = require("path");

const CORE =
  process.platform === "win32"
    ? path.join(__dirname, "core.exe")
    : path.join(__dirname, "core");

const SENDER =
  process.platform === "win32"
    ? path.join(__dirname, "sender.exe")
    : path.join(__dirname, "sender");

// --- SPAWN CORE ENGINE ---
const coreEngine = spawn(CORE);

coreEngine.stdout.on("data", (data) => {
  // When C++ core prints to stdout, this fires
  // data is a Buffer, needs .toString()

  // C++ core outputs:
  // "Founded Peer: DESKTOP:192.168.1.10 Alive"
  const lines = data.toString().trim().split("\n");

  lines.forEach((line) => {
    if (line.includes("Discovered peer:")) {
      const parts = line.split("Discovered peer: ");

      if (parts.length > 1) {
        const raw = parts[1].replace(" Alive", "").trim();

        if (raw.includes(":")) {
          const [name, ip] = raw.split(":");

          // 🔥 IGNORE INVALID, LOCALHOST, AND MYSELF
          if (!ip || ip === "127.0.0.1" || myIps.includes(ip)) return;

          // 🔥 STORE UNIQUE
          peers.set(ip, name);
          console.log("Map size:", peers.size);
          console.log("Peers map:", Array.from(peers.entries()));

          const peerList = Array.from(peers.entries()).map(([ip, name]) => ({
            name,
            ip,
          }));

          console.log("🟢 Active Devices:", peerList);

          // 🔥 SEND FULL LIST
          console.log("Emitting peers_list:", peerList);
          io.emit("peers_list", peerList);
        }
      }
    } else if (line.trim().length > 0) {
      console.log(`⚙️ [C++] ${line.trim()}`);
    }
  });
});

// --- FILE UPLOAD & SEND ROUTE ---
app.post("/send", upload.single("file"), (req, res) => {
  const { targetIp, requestMode } = req.body;
  const filePath = req.file.path;
  const fileSize = req.file.size;
  const filename = req.file.originalname;

  if (!targetIp) {
    return res.status(400).json({ error: "Target IP missing" });
  }

  console.log(`🚀 Uploading ${filename} (${fileSize} bytes) → ${targetIp}`);

  // 🔥 NEW FLOW: Store file path for request mode
  if (requestMode === "true") {
    // Store file path for later use when request is accepted
    uploadedFiles.set(targetIp, {
      filename: filename,
      filePath: filePath,
      fileSize: fileSize,
      uploadedAt: Date.now(),
    });

    console.log(`📦 File stored for request mode: ${filename}`);
    return res.json({ success: true, filename: filename });
  }

  // 🔥 LEGACY FLOW: Send immediately (backward compatible)
  const sender = spawn(SENDER, [targetIp, filePath, fileSize.toString()]);

  sender.stdout.on("data", (data) => {
    console.log(`📤 [SENDER]: ${data.toString()}`);
  });

  sender.stderr.on("data", (data) => {
    console.error(`❌ [SENDER ERROR]: ${data.toString()}`);
  });

  sender.on("close", (code) => {
    console.log(`🏁 Sender finished (Code: ${code})`);

    if (fs.existsSync(filePath)) {
      fs.unlinkSync(filePath);
    }

    res.json({ success: code === 0 });
  });
});

// --- TRANSFER REQUEST ENDPOINT ---
// Initiates request flow WITHOUT starting sender.cpp immediately
app.post("/send-request", (req, res) => {
  const { targetIp, filename, fileSize, fileType } = req.body;

  if (!targetIp || !filename || !fileSize) {
    return res.status(400).json({ error: "Missing required fields" });
  }

  // Check if target is online
  if (!peers.has(targetIp)) {
    return res
      .status(404)
      .json({ error: "Target device not found on network" });
  }

  console.log(`📨 Sending transfer request: ${filename} to ${targetIp}`);

  // Create request ID
  const requestId = `req_${Date.now()}_${Math.random().toString(36).substr(2, 9)}`;
  const senderName = "This Device"; // Could get from system

  // Store pending request
  pendingRequests.set(requestId, {
    id: requestId,
    senderIp: "local",
    senderName: senderName,
    targetIp: targetIp,
    filename: filename,
    fileSize: fileSize,
    fileType: fileType,
    status: "pending",
    createdAt: Date.now(),
  });

  // Broadcast to receiver (targetIp)
  io.to(targetIp).emit("incoming_request", {
    requestId,
    senderName: senderName,
    senderIp: "local",
    filename,
    fileSize,
    fileType,
  });

  // Set timeout (30 seconds)
  const timeout = setTimeout(() => {
    if (pendingRequests.has(requestId)) {
      pendingRequests.delete(requestId);
      // Emit rejection via socket is handled separately
    }
  }, 30000);
  requestTimeouts.set(requestId, timeout);

  res.json({ requestId, status: "pending" });
});

// --- HTTP ENDPOINT TO START SENDER.CPP (called after accept) ---
app.post("/start-transfer", (req, res) => {
  const { requestId, targetIp } = req.body;

  const request = pendingRequests.get(requestId);
  if (!request) {
    return res.status(404).json({ error: "Request not found" });
  }

  // Get target IP (from request or parameter)
  const target = targetIp || request.targetIp;

  // Get the uploaded file from storage
  const uploadedFile = uploadedFiles.get(target);
  if (!uploadedFile) {
    return res
      .status(404)
      .json({ error: "File not found - please upload first" });
  }

  const { filePath, fileSize, filename } = uploadedFile;

  console.log(`🚀 Starting transfer for request: ${requestId}`);

  if (!fs.existsSync(filePath)) {
    return res.status(404).json({ error: "File not found for transfer" });
  }

  const sender = spawn(SENDER, [target, filePath, fileSize.toString()]);

  sender.stdout.on("data", (data) => {
    console.log(`📤 [SENDER]: ${data.toString()}`);
  });

  sender.stderr.on("data", (data) => {
    console.error(`❌ [SENDER ERROR]: ${data.toString()}`);
  });

  sender.on("close", (code) => {
    console.log(`🏁 Sender finished (Code: ${code})`);

    if (fs.existsSync(filePath)) {
      fs.unlinkSync(filePath);
    }

    // Clean up uploaded file record
    uploadedFiles.delete(target);

    // If successful, notify receiver that transfer is complete
    // Receiver will then confirm delivery back to sender
    if (code === 0) {
      io.to(target).emit("transfer_complete", { requestId });
    }

    // Clean up request
    pendingRequests.delete(requestId);
  });

  res.json({ success: true, requestId });
});

// --- SOCKET CONNECTION ---
io.on("connection", (socket) => {
  console.log("🔌 Client connected");

  // send current list immediately
  // This ensures new clients see existing peers immediately
  const peerList = Array.from(peers.entries()).map(([ip, name]) => ({
    name,
    ip,
  }));

  socket.emit("peers_list", peerList);

  // 🔥 HANDLE TRANSFER REQUEST (from sender)
  socket.on("transfer_request", (data) => {
    const { targetIp, filename, fileSize, fileType } = data;
    // Ensure clean IP (trim whitespace)
    const cleanTargetIp = targetIp ? targetIp.trim() : targetIp;
    const requestId = `req_${Date.now()}_${Math.random().toString(36).substr(2, 9)}`;

    // Get sender info
    const senderName = socket.ip || "Unknown";

    console.log("========== TRANSFER REQUEST DEBUG ==========");
    console.log(`📨 [1] transfer_request received`);
    console.log(`   targetIp: "${cleanTargetIp}" (type: ${typeof cleanTargetIp})`);
    console.log(`   filename: ${filename}`);
    console.log(`   sender socket.id: ${socket.id}`);
    console.log(`   sender socket.ip: ${socket.ip}`);
    console.log(`   sender handshake address: ${socket.handshake.address}`);

    // Debug: List ALL sockets and their rooms
    console.log(`   [2] ALL sockets:`);
    for (const [id, sock] of io.sockets.sockets) {
      console.log(`      socket ${id}: ip=${sock.ip}, rooms=${Array.from(sock.rooms).join(',')}`);
    }

    // Debug: List all IP rooms (including exact match)
    console.log(`   [3] All IP rooms:`);
    for (const [roomName, sockets] of io.sockets.adapter.rooms) {
      if (roomName.includes('.') || /^192\./.test(roomName)) {
        console.log(`      Room "${roomName}": ${Array.from(sockets).join(', ')}`);
      }
    }

    // Debug: Also check raw rooms map
    console.log(`   [3b] Raw rooms check:`);
    const allRooms = io.sockets.adapter.rooms;
    for (const [roomName, sockets] of allRooms) {
      if (typeof roomName === 'string' && roomName.length > 6) {
        console.log(`      "${roomName}" -> ${Array.from(sockets).join(', ')}`);
      }
    }

    // Check exact room match
    const room = io.sockets.adapter.rooms.get(cleanTargetIp);
    console.log(`   [4] Exact room lookup for "${cleanTargetIp}": ${room ? Array.from(room).join(', ') : 'NOT FOUND'}`);

    // Also try finding by iterating
    let foundRoom = null;
    for (const [name, socks] of io.sockets.adapter.rooms) {
      if (name === cleanTargetIp) {
        foundRoom = socks;
        break;
      }
    }
    console.log(`   [4b] Iterated room lookup: ${foundRoom ? Array.from(foundRoom).join(', ') : 'NOT FOUND'}`);

    // Store pending request with socket ID for direct response
    pendingRequests.set(requestId, {
      id: requestId,
      senderSocketId: socket.id,
      senderIp: socket.ip,
      senderName: senderName,
      targetIp: cleanTargetIp,
      filename: filename,
      fileSize: fileSize,
      fileType: fileType,
      status: "pending",
      createdAt: Date.now(),
    });

    console.log(`   [5] Emitting incoming_request to room "${cleanTargetIp}"...`);

    // Check if any sockets are in that room BEFORE emitting
    let socketsInRoom = io.sockets.adapter.rooms.get(cleanTargetIp);
    if (!socketsInRoom || socketsInRoom.size === 0) {
      console.log(`   ⚠️ WARNING: Room "${cleanTargetIp}" is EMPTY!`);

      // 🔥 Check if receiver is connected at all
      let receiverSocket = null;
      for (const [sockId, sock] of io.sockets.sockets) {
        if (sock.ip === cleanTargetIp) {
          receiverSocket = sockId;
          break;
        }
      }

      if (!receiverSocket) {
        console.log(`   ❌ RECEIVER OFFLINE: No socket with IP ${cleanTargetIp}`);
        console.log(`   📡 Connected sockets:`);
        for (const [sockId, sock] of io.sockets.sockets) {
          console.log(`      ${sockId}: ip=${sock.ip}`);
        }
        console.log("========== END DEBUG ==========");

        // Notify sender that receiver is offline
        socket.emit("request_rejected", {
          requestId,
          reason: "Receiver is offline or disconnected",
        });
        pendingRequests.delete(requestId);
        return;
      }

      console.log(`   ✅ Found receiver by socket.ip! socket.id=${receiverSocket}`);
      io.to(receiverSocket).emit("incoming_request", {
        requestId,
        senderName: senderName,
        senderIp: socket.ip,
        filename,
        fileSize,
        fileType,
      });
      console.log(`   [6] Fallback emit done.`);
      console.log("========== END DEBUG ==========");

      // Set timeout (30 seconds)
      const timeout = setTimeout(() => {
        if (pendingRequests.has(requestId)) {
          pendingRequests.delete(requestId);
          socket.emit("request_rejected", {
            requestId,
            reason: "Request timed out",
          });
        }
      }, 30000);
      requestTimeouts.set(requestId, timeout);

      // Send request ID back to sender so they can track it
      socket.emit("request_queued", { requestId });
      return; // Exit early, don't do normal emit
    } else {
      console.log(`   ✅ Room has ${socketsInRoom.size} socket(s): ${Array.from(socketsInRoom).join(', ')}`);
    }

    // Broadcast to receiver (cleanTargetIp)
    const emitResult = io.to(cleanTargetIp).emit("incoming_request", {
      requestId,
      senderName: senderName,
      senderIp: socket.ip,
      filename,
      fileSize,
      fileType,
    });

    console.log(`   [6] Emit result: ${emitResult ? 'success' : 'failed/no-recipients'}`);
    console.log(`   pendingRequests now has ${pendingRequests.size} entries`);
    console.log("========== END DEBUG ==========");

    // Set timeout (30 seconds)
    const timeout = setTimeout(() => {
      if (pendingRequests.has(requestId)) {
        pendingRequests.delete(requestId);
        socket.emit("request_rejected", {
          requestId,
          reason: "Request timed out",
        });
      }
    }, 30000);
    requestTimeouts.set(requestId, timeout);

    // Send request ID back to sender so they can track it
    socket.emit("request_queued", { requestId });
  });

  // 🔥 HANDLE TRANSFER ACCEPT (from receiver)
  socket.on("transfer_accept", (data) => {
    const { requestId, senderIp } = data;
    console.log(`✅ Transfer accepted: ${requestId}`);

    const request = pendingRequests.get(requestId);
    if (!request) {
      console.log(`⚠️ Request not found: ${requestId}`);
      return;
    }

    // Clear timeout
    const timeout = requestTimeouts.get(requestId);
    if (timeout) {
      clearTimeout(timeout);
      requestTimeouts.delete(requestId);
    }

    // Update status
    request.status = "accepted";
    request.acceptedAt = Date.now();

    // 🔥 Notify sender directly via socket ID
    if (request.senderSocketId) {
      io.to(request.senderSocketId).emit("request_accepted", {
        requestId,
        targetIp: request.targetIp,
      });
    } else {
      // Fallback: emit to all
      io.emit("request_accepted", { requestId, targetIp: request.targetIp });
    }
  });

  // 🔥 HANDLE TRANSFER REJECT (from receiver)
  socket.on("transfer_reject", (data) => {
    const { requestId, senderIp, reason } = data;
    console.log(
      `❌ Transfer rejected: ${requestId} - ${reason || "Rejected by user"}`,
    );

    const request = pendingRequests.get(requestId);
    if (!request) return;

    // Clear timeout
    const timeout = requestTimeouts.get(requestId);
    if (timeout) {
      clearTimeout(timeout);
      requestTimeouts.delete(requestId);
    }

    // Update status
    request.status = "rejected";
    request.rejectedAt = Date.now();
    request.rejectReason = reason;

    // Notify sender
    if (request.senderSocketId) {
      io.to(request.senderSocketId).emit("request_rejected", {
        requestId,
        reason: reason || "Rejected by user",
      });
    }
    // Clean up
    pendingRequests.delete(requestId);
  });

  // 🔥 HANDLE TRANSFER DELIVERED (from receiver after successful transfer)
  socket.on("transfer_delivered", (data) => {
    const { requestId } = data;
    console.log(`🎉 Transfer delivered confirmation: ${requestId}`);

    const request = pendingRequests.get(requestId);
    if (!request) {
      console.log(`⚠️ Request not found for delivery: ${requestId}`);
      return;
    }

    // Forward to sender
    if (request.senderSocketId) {
      io.to(request.senderSocketId).emit("transfer_delivered", { requestId });
    } else {
      io.emit("transfer_delivered", { requestId });
    }

    // Clean up
    pendingRequests.delete(requestId);
  });

  // 🔥 HANDLE TRANSFER PROGRESS
  socket.on("transfer_progress", (data) => {
    const { requestId, progress, senderIp } = data;

    // Forward to sender
    io.to(senderIp).emit("transfer_progress", { requestId, progress });
  });

  // 🔥 HANDLE TRANSFER CANCEL (from sender)
  socket.on("transfer_cancel", (data) => {
    const { requestId, targetIp } = data;
    console.log(`🚫 Transfer cancelled: ${requestId}`);

    const request = pendingRequests.get(requestId);
    if (request) {
      // Clear timeout
      const timeout = requestTimeouts.get(requestId);
      if (timeout) {
        clearTimeout(timeout);
        requestTimeouts.delete(requestId);
      }

      // Notify receiver
      io.to(targetIp).emit("request_cancelled", { requestId });

      // Clean up
      pendingRequests.delete(requestId);
    }
  });

  // Store socket IP for targeting
  socket.on("register", (data) => {
    const { ip } = data;
    // Trim whitespace to ensure clean room names
    const cleanIp = ip ? ip.trim() : ip;
    socket.ip = cleanIp;
    socket.join(cleanIp);
    console.log(`📱 Client registered with IP: ${cleanIp}`);
    console.log(`🔗 ${cleanIp} -> ${socket.id}`);

    // Debug: show all rooms after registration
    console.log(`   [DEBUG] All IP rooms after registration:`);
    for (const [roomName, sockets] of io.sockets.adapter.rooms) {
      if (roomName.includes('.') || /^192\./.test(roomName)) {
        console.log(`      Room "${roomName}": ${Array.from(sockets).join(', ')}`);
      }
    }
  });

  // Handle disconnect
  socket.on("disconnect", (reason) => {
    console.log(`🔌 Client disconnected: ${socket.id} (${reason})`);
    console.log(`   Was registered with IP: ${socket.ip}`);
    console.log(`   Active IP rooms after disconnect:`);
    for (const [roomName, sockets] of io.sockets.adapter.rooms) {
      if (roomName.includes('.') || /^192\./.test(roomName)) {
        console.log(`      Room "${roomName}": ${Array.from(sockets).join(', ')}`);
      }
    }
    // Clean up any pending requests from this client
    for (const [reqId, request] of pendingRequests.entries()) {
      if (request.senderSocketId === socket.id) {
        const timeout = requestTimeouts.get(reqId);
        if (timeout) {
          clearTimeout(timeout);
          requestTimeouts.delete(reqId);
        }
        pendingRequests.delete(reqId);
      }
    }
  });
});

server.listen(5001, () =>
  console.log("✅ Lighthouse Backend at http://localhost:5001"),
);
