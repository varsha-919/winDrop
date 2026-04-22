const express = require('express');
const http = require('http');
const { Server } = require('socket.io');
const cors = require('cors');
const { spawn } = require('child_process');

const app = express();
app.use(cors());
const server = http.createServer(app);

const io = new Server(server, {
    cors: { origin: "*", methods: ["GET", "POST"] }
});

const engine = spawn('./core');

engine.stdout.on('data', (data) => {
    const outputLines = data.toString().trim().split('\n');
    
    outputLines.forEach(line => {
        if (line.includes('Founded Peer:')) {
            
            const rawInfo = line.split('Founded Peer: ')[1].replace(' Alive', '').trim();
            
            if (rawInfo.includes(':')) {
                const [name, ip] = rawInfo.split(':');
                console.log(`Emitting to React -> Name: ${name}, IP: ${ip}`);
                
                io.emit('peer_discovered', { name, ip });
            }
        } 
        else if (line.trim().length > 0) {
            console.log(`${line.trim()}`);
        }
    });
});
engine.stderr.on('data', (data) => {
    console.error(`Error: ${data}`);
});

io.on('connection', (socket) => {
    console.log(`React Frontend Socket ID: ${socket.id}`);
});

const PORT = 5000;
server.listen(PORT, () => {
    console.log(`Connected at url: http://localhost:${PORT}`);
});