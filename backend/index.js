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

const server = http.createServer(app);
const io = new Server(server, { cors: { origin: "*" } });

// 🔥 STORE UNIQUE PEERS
const peers = new Map();

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
  const { targetIp } = req.body;
  const filePath = req.file.path;
  const fileSize = req.file.size; // Get file size from multer

  if (!targetIp) {
    return res.status(400).json({ error: "Target IP missing" });
  }

  console.log(`🚀 Sending ${req.file.originalname} (${fileSize} bytes) → ${targetIp}`);

  // Pass file size to sender for resume metadata support
  const sender = spawn(SENDER, [targetIp, filePath, fileSize.toString()]);

  sender.stdout.on("data", (data) => {
    // Output from sender
    console.log(`📤 [SENDER]: ${data.toString()}`);
  });

  sender.stderr.on("data", (data) => {
    // Error output
    console.error(`❌ [SENDER ERROR]: ${data.toString()}`);
  });

  sender.on("close", (code) => {
    // When process exits code: 0 = success, non-zero = error
    console.log(`🏁 Sender finished (Code: ${code})`);

    if (fs.existsSync(filePath)) {
      fs.unlinkSync(filePath);
    }
    // Delete uploaded file after sending
    // Prevents disk space from filling up

    res.json({ success: code === 0 });
  });
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
});

server.listen(5001, () =>
  console.log("✅ Lighthouse Backend at http://localhost:5001"),
);
