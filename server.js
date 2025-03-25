require('dotenv').config();
const express = require('express');
const http = require('http');
const path = require('path');
const cors = require('cors');
const { User, Game } = require('./models');
const setupSockets = require('./sockets');
const fs = require('fs');

const app = express();
const server = http.createServer(app);
const io = require('socket.io')(server, {
  cors: {
    origin: process.env.CLIENT_URL || "*",
    methods: ["GET", "POST"]
  }
});

// Ensure persistent directory exists
const dbDir = path.join(__dirname, 'persistent');
if (!fs.existsSync(dbDir)) {
  fs.mkdirSync(dbDir, { recursive: true });
  console.log('Created persistent storage directory');
}

// Middleware
app.use(cors());
app.use(express.json());
app.use(express.static(path.join(__dirname, 'public')));

// Routes
app.post('/create-game', async (req, res) => {
  try {
    const game = await Game.create();
    res.json({ shortCode: game.shortCode });
  } catch (error) {
    res.status(500).json({ error: error.message });
  }
});

app.get('/game/:shortCode', async (req, res) => {
  try {
    const game = await Game.getByCode(req.params.shortCode);
    if (!game) {
      return res.status(404).json({ error: 'Game not found' });
    }
    res.json(game);
  } catch (error) {
    res.status(500).json({ error: error.message });
  }
});

app.post('/auth', async (req, res) => {
  try {
    const { username, password, isNew } = req.body;
    
    let user;
    if (isNew) {
      const existing = await User.findByName(username);
      if (existing) {
        return res.status(400).json({ error: 'Username already exists' });
      }
      const userId = await User.create(username, password);
      user = { id: userId, username };
    } else {
      user = await User.findByName(username);
      if (!user || !(await User.comparePassword(password, user.password))) {
        return res.status(401).json({ error: 'Invalid credentials' });
      }
    }
    
    res.json({ user: { id: user.id, username: user.username } });
  } catch (error) {
    res.status(500).json({ error: error.message });
  }
});

app.get('/stats/:username', async (req, res) => {
  try {
    const stats = await User.getStats(req.params.username);
    if (!stats) {
      return res.status(404).json({ error: 'User not found' });
    }
    res.json(stats);
  } catch (error) {
    res.status(500).json({ error: error.message });
  }
});

// Setup Socket.io
setupSockets(io);

const PORT = process.env.PORT || 3000;
server.listen(PORT, () => {
  console.log(`Server running on port ${PORT}`);
});