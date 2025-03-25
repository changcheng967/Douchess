import express from 'express';
import http from 'http';
import path from 'path';
import { fileURLToPath } from 'url';
import cors from 'cors';
import { User, Game } from './models.js';
import setupSockets from './sockets.js';
import fs from 'fs';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

// Ensure persistent directory exists
const dbDir = path.join(__dirname, 'persistent');
if (!fs.existsSync(dbDir)) {
  fs.mkdirSync(dbDir, { recursive: true });
  console.log('Created persistent storage directory');
}

const app = express();
const server = http.createServer(app);
const io = new SocketIO.Server(server, {
  cors: {
    origin: process.env.CLIENT_URL || "*",
    methods: ["GET", "POST"]
  }
});

app.use(cors());
app.use(express.json());
app.use(express.static(path.join(__dirname, 'public')));

// Routes
app.post('/create-game', async (req, res) => {
  try {
    const game = await Game.create();
    console.log('Game created:', game.shortCode);
    res.json({ shortCode: game.shortCode });
  } catch (error) {
    console.error('Create game error:', error);
    res.status(500).json({ error: error.message });
  }
});

// ... keep other routes the same ...

// Initialize sockets
setupSockets(io);

const PORT = process.env.PORT || 3000;
server.listen(PORT, () => {
  console.log(`Server running on port ${PORT}`);
});
