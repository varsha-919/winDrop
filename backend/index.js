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
app.use(express.json());

// Return this machine's LAN IP
app.get("/my-ip", (req, res) => {
  res.json({
    ip: myIps[0] || "127.0.0.1",
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

// --- MONITOR FOR INCOMING REQUEST FILES ---
// Core.cpp writes requests to /tmp/windrop_request_<id>.json
// We poll for these files and emit to frontend
const { watch } = require("fs");

let requestWatcher = null;

function startRequestMonitor() {
  const tempDir = "/tmp";

  // Poll every 1 second for new request files
  // pick requets from /tmp and add in pending requests map
  setInterval(() => {
    try {
      const files = fs.readdirSync(tempDir);
      files.forEach((file) => {
        if (file.startsWith("windrop_request_") && file.endsWith(".json")) {
          const filePath = path.join(tempDir, file);

          try {
            const content = fs.readFileSync(filePath, "utf8");
            const requestData = JSON.parse(content);

            console.log("📨 Incoming request:", requestData);

            // Emit to local frontend via Socket.IO
            io.emit("incoming_request", requestData);

            // Store pending request
            pendingRequests.set(requestData.requestId, {
              id: requestData.requestId,
              senderIp: requestData.senderIP,
              filename: requestData.filename,
              fileSize: requestData.fileSize,
              fileType: requestData.fileType,
              status: "pending",
              createdAt: Date.now(),
            });

            // Remove the file after processing
            fs.unlinkSync(filePath);
            console.log("📝 Request file processed and removed");
          } catch (err) {
            console.error("Error processing request file:", err);
          }
        }
      });
    } catch (err) {
      // Directory doesn't exist yet, ignore
    }
  }, 1000);

  console.log("🔍 Request monitor started");
}

// Start the request monitor
startRequestMonitor();

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
//requestMode - Either "true" (request/response workflow) or undefined (legacy direct send)
//Why two modes:
// Request mode: More user-friendly - receiver gets popup, can accept/reject
// Legacy mode: Direct send without asking - receiver just receives the file

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

  //  LEGACY FLOW: Send immediately (backward compatible)
  const sender = spawn(SENDER, [targetIp, filePath, fileSize.toString()]);

  sender.stdout.on("data", (data) => {
    console.log(`[SENDER]: ${data.toString()}`);
  });

  sender.stderr.on("data", (data) => {
    console.error(` [SENDER ERROR]: ${data.toString()}`);
  });

  sender.on("close", (code) => {
    console.log(` Sender finished (Code: ${code})`);

    if (fs.existsSync(filePath)) {
      fs.unlinkSync(filePath);
    }

    res.json({ success: code === 0 });
  });
});

// --- TRANSFER REQUEST ENDPOINT ---
// Spawns sender.cpp in request mode - communicates via TCP
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

  // Get the uploaded file
  const uploadedFile = uploadedFiles.get(targetIp);
  if (!uploadedFile) {
    return res.status(400).json({ error: "No file uploaded. Upload first." });
  }

  const { filePath, fileSize: storedSize } = uploadedFile;

  // Create request ID
  const requestId = `req_${Date.now()}_${Math.random().toString(36).substr(2, 9)}`;

  console.log(
    `📨 Sending TCP-based transfer request: ${filename} to ${targetIp}`,
  );

  // Store pending request
  pendingRequests.set(requestId, {
    id: requestId,
    senderIp: "local",
    targetIp: targetIp,
    filename: filename,
    fileSize: fileSize,
    fileType: fileType,
    status: "pending",
    createdAt: Date.now(),
  });

  // Spawn sender in REQUEST mode
  // Usage: ./sender --request <target_ip> <request_id> <file_path> [file_size]
  const sender = spawn(SENDER, [
    "--request",
    targetIp,
    requestId,
    filePath,
    fileSize.toString(),
  ]);

  let senderOutput = "";

  sender.stdout.on("data", (data) => {
    const output = data.toString();
    senderOutput += output;
    console.log(`📤 [SENDER]: ${output}`);
  });

  sender.stderr.on("data", (data) => {
    console.error(`❌ [SENDER ERROR]: ${data.toString()}`);
  });

  sender.on("close", (code) => {
    console.log(`🏁 Sender finished (Code: ${code})`);
    console.log(`📤 Output: ${senderOutput}`);

    // Parse output to check for accept/reject
    // OLD IMPLEMENTATION - COMMENTED OUT: These emits are part of the old Socket.IO workflow.
    // In the new TCP flow, the sender stays alive and handles the complete transfer.
    // The frontend should rely on the transfer completion status, not these notifications.
    // Also, these emits fire when sender.cpp exits, which happens AFTER the transfer
    // completes in the TCP flow, making them redundant and potentially confusing.
    /*
    if (senderOutput.includes("accepted")) {
      pendingRequests.get(requestId).status = "accepted";
      // Notify frontend of acceptance
      io.emit("request_accepted", { requestId, targetIp });
    } else if (senderOutput.includes("rejected")) {
      pendingRequests.get(requestId).status = "rejected";
      // Notify frontend of rejection
      io.emit("request_rejected", { requestId, reason: "Rejected by user" });
    }
    */

    // Clean up uploaded file record
    uploadedFiles.delete(targetIp);
  });

  // Return immediately - sender runs asynchronously
  res.json({ requestId, status: "pending" });
});

// --- RESPOND TO TRANSFER REQUEST (ACCEPT/REJECT) ---
// Writes response to /tmp/windrop_response_<requestId>.txt for core.cpp to read
app.post("/respond-request", (req, res) => {
  const { requestId, action } = req.body;

  if (!requestId || !action) {
    return res.status(400).json({ error: "Missing requestId or action" });
  }

  if (action !== "ACCEPT" && action !== "REJECT") {
    return res.status(400).json({ error: "Invalid action" });
  }

  console.log(`📝 Responding to request ${requestId}: ${action}`);

  // Write response to file for core.cpp to read
  const responseFile = `/tmp/windrop_response_${requestId}.txt`;
  try {
    fs.writeFileSync(responseFile, action);
    console.log(`📝 Response written to: ${responseFile}`);

    // Update request status
    const request = pendingRequests.get(requestId);
    if (request) {
      request.status = action === "ACCEPT" ? "accepted" : "rejected";
    }

    res.json({ success: true });
  } catch (err) {
    console.error("Failed to write response file:", err);
    res.status(500).json({ error: "Failed to respond" });
  }
});

// --- HTTP ENDPOINT TO START SENDER.CPP (called after accept) ---
// OLD IMPLEMENTATION - COMMENTED OUT: Replaced by TCP automatic continuation in sender.cpp
// In the new TCP workflow, sender.cpp stays alive and automatically continues the transfer
// after receiving ACCEPT via TCP. No second sender process should be spawned.
/*
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
*/

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
  // OLD IMPLEMENTATION - COMMENTED OUT: Replaced by TCP request/response workflow.
  // In the new flow, sender.cpp communicates via TCP with core.cpp, which writes
  // request.json files. The request monitor detects these and emits "incoming_request".
  /*
  socket.on("transfer_request", (data) => {
    const { targetIp, filename, fileSize, fileType } = data;
    const requestId = `req_${Date.now()}_${Math.random().toString(36).substr(2, 9)}`;

    // Get sender info
    const senderName = peers.get(socket.handshake.address) || "Unknown";

    console.log(
      `📨 Transfer request: ${filename} (${fileSize} bytes) from ${targetIp}`,
    );

    // Store pending request with socket ID for direct response
    pendingRequests.set(requestId, {
      id: requestId,
      senderSocketId: socket.id,
      senderIp: socket.handshake.address,
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
      senderIp: socket.handshake.address,
      filename,
      fileSize,
      fileType,
    });

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
  */

  // 🔥 HANDLE TRANSFER ACCEPT (from receiver)
  // OLD IMPLEMENTATION - COMMENTED OUT: Replaced by TCP request/response workflow.
  // In the new flow, the receiver responds via HTTP (/respond-request), which writes
  // response.txt. The sender.cpp reads this via TCP and continues automatically.
  /*
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
  */

  // 🔥 HANDLE TRANSFER REJECT (from receiver)
  // OLD IMPLEMENTATION - COMMENTED OUT: Replaced by TCP request/response workflow.
  /*
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
    io.to(senderIp).emit("request_rejected", {
      requestId,
      reason: reason || "Rejected by user",
    });

    // Clean up
    pendingRequests.delete(requestId);
  });
  */

  // 🔥 HANDLE TRANSFER DELIVERED (from receiver after successful transfer)
  // OLD IMPLEMENTATION - COMMENTED OUT: Replaced by TCP COMPLETE/DELIVERED_ACK protocol.
  /*
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
  */

  // 🔥 HANDLE TRANSFER PROGRESS
  // OLD IMPLEMENTATION - COMMENTED OUT: Progress is now handled via TCP protocol.
  /*
  socket.on("transfer_progress", (data) => {
    const { requestId, progress, senderIp } = data;

    // Forward to sender
    io.to(senderIp).emit("transfer_progress", { requestId, progress });
  });
  */

  // 🔥 HANDLE TRANSFER CANCEL (from sender)
  // OLD IMPLEMENTATION - COMMENTED OUT: Cancel workflow is now handled via TCP.
  /*
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
  */

  // Store socket IP for targeting
  socket.on("register", (data) => {
    const { ip } = data;
    socket.ip = ip;
    socket.join(ip);
    console.log(`📱 Client registered with IP: ${ip}`);
  });

  // Handle disconnect
  socket.on("disconnect", () => {
    console.log("🔌 Client disconnected");
    // Clean up any pending requests from this client
    for (const [reqId, request] of pendingRequests.entries()) {
      if (request.senderIp === socket.handshake.address) {
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
