const express = require('express');
const http = require('http');
const { Server } = require('socket.io');
const cors = require('cors');
const { spawn } = require('child_process');
const multer = require('multer');
const fs = require('fs');

const app = express();
app.use(cors());
app.use(express.json());

const server = http.createServer(app);
const io = new Server(server, { cors: { origin: "*" } });

const peers = new Map();

const storage = multer.diskStorage({
    destination: (req, file, cb) => {
        if (!fs.existsSync('./uploads')) fs.mkdirSync('./uploads');
        cb(null, './uploads');
    },
    filename: (req, file, cb) => cb(null, file.originalname)
});
const upload = multer({ storage: storage });
const coreEngine = spawn('./core');

coreEngine.stdout.on('data', (data) => {
    const lines = data.toString().trim().split('\n');

    lines.forEach(line => {

        if (line.includes('Founded Peer:')) {
            const parts = line.split('Founded Peer: ');

            if (parts.length > 1) {
                const raw = parts[1].replace(' Alive', '').trim();

                if (raw.includes(':')) {
                    const [name, ip] = raw.split(':');

                    if (!ip || ip === "127.0.0.1") return;

                    peers.set(ip, name);

                    const peerList = Array.from(peers.entries()).map(([ip, name]) => ({
                        name,
                        ip
                    }));

                    console.log("🟢 Active Devices:", peerList);

                    io.emit('peers_list', peerList);
                }
            }

        } else if (line.trim().length > 0) {
            console.log(`[C++] ${line.trim()}`);
        }
    });
});

// file upload
app.post('/send', upload.single('file'), (req, res) => {
    const { targetIp } = req.body;
    const filePath = req.file.path;

    if (!targetIp) {
        return res.status(400).json({ error: "Target IP missing" });
    }

    console.log(`🚀 Sending ${req.file.originalname} → ${targetIp}`);

    const sender = spawn('./sender', [targetIp, filePath]);

    sender.stdout.on("data", (data) => {
        console.log(`[SENDER]: ${data.toString()}`);
    });

    sender.stderr.on("data", (data) => {
        console.error(`[SENDER ERROR]: ${data.toString()}`);
    });

    sender.on('close', (code) => {
        console.log(`Sender finished (Code: ${code})`);

        if (fs.existsSync(filePath)) {
            fs.unlinkSync(filePath);
        }

        res.json({ success: code === 0 });
    });
});

// socket
io.on("connection", (socket) => {
    console.log("🔌 Client connected");

    const peerList = Array.from(peers.entries()).map(([ip, name]) => ({
        name,
        ip
    }));

    socket.emit("peers_list", peerList);
});

server.listen(5000, () =>
    console.log('✅ Lighthouse Backend at http://localhost:5000')
);