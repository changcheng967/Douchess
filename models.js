import { Chess } from 'chess.js';
import shortid from 'shortid';
import db from './database.js';

// User model
const User = {
  async create(username, password = null) {
    const hashedPassword = password ? await bcryptjs.hash(password, 10) : null;
    return new Promise((resolve, reject) => {
      db.run(
        'INSERT INTO users (username, password) VALUES (?, ?)',
        [username, hashedPassword],
        function(err) {
          if (err) return reject(err);
          resolve(this.lastID);
        }
      );
    });
  },

  async findByName(username) {
    return new Promise((resolve, reject) => {
      db.get('SELECT * FROM users WHERE username = ?', [username], (err, row) => {
        if (err) return reject(err);
        resolve(row);
      });
    });
  },

  async comparePassword(password, hashedPassword) {
    if (!hashedPassword) return false;
    return await bcrypt.compare(password, hashedPassword);
  },

  async updateStats(userId, result) {
    const field = result === 'win' ? 'wins' : result === 'loss' ? 'losses' : 'draws';
    return new Promise((resolve, reject) => {
      db.run(
        `UPDATE users SET ${field} = ${field} + 1 WHERE id = ?`,
        [userId],
        function(err) {
          if (err) return reject(err);
          resolve(this.changes);
        }
      );
    });
  },

  async getStats(username) {
    return new Promise((resolve, reject) => {
      db.get(
        'SELECT username, wins, losses, draws, elo FROM users WHERE username = ?',
        [username],
        (err, row) => {
          if (err) return reject(err);
          resolve(row);
        }
      );
    });
  }
};

// Game model
const Game = {
  async create() {
    const shortCode = shortid.generate();
    return new Promise((resolve, reject) => {
      db.run(
        'INSERT INTO games (short_code) VALUES (?)',
        [shortCode],
        function(err) {
          if (err) return reject(err);
          resolve({ id: this.lastID, shortCode });
        }
      );
    });
  },

  join(gameId, playerId, username, color) {
    const column = color === 'white' ? 'white_id' : 'black_id';
    const nameColumn = color === 'white' ? 'white_name' : 'black_name';
    return new Promise((resolve, reject) => {
      db.run(
        `UPDATE games SET ${column} = ?, ${nameColumn} = ?, status = ? WHERE id = ?`,
        [playerId, username, 'active', gameId],
        function(err) {
          if (err) return reject(err);
          resolve(this.changes);
        }
      );
    });
  },

  makeMove(gameId, fen, pgn) {
    return new Promise((resolve, reject) => {
      const chess = new Chess(fen);
      const status = chess.isGameOver() 
        ? chess.isCheckmate() 
          ? chess.turn() === 'w' ? 'black_win' : 'white_win'
          : 'draw'
        : 'active';

      db.run(
        'UPDATE games SET fen = ?, pgn = ?, status = ? WHERE id = ?',
        [fen, pgn, status, gameId],
        function(err) {
          if (err) return reject(err);
          resolve(status);
        }
      );
    });
  },

  getByCode(shortCode) {
    return new Promise((resolve, reject) => {
      db.get('SELECT * FROM games WHERE short_code = ?', [shortCode], (err, row) => {
        if (err) return reject(err);
        resolve(row);
      });
    });
  },

  get(gameId) {
    return new Promise((resolve, reject) => {
      db.get('SELECT * FROM games WHERE id = ?', [gameId], (err, row) => {
        if (err) return reject(err);
        resolve(row);
      });
    });
  },

  listAvailable() {
    return new Promise((resolve, reject) => {
      db.all(`
        SELECT g.id, g.short_code, g.created_at, u.username as white_username
        FROM games g
        JOIN users u ON g.white_id = u.id
        WHERE g.black_id IS NULL
        ORDER BY g.created_at DESC
      `, (err, rows) => {
        if (err) return reject(err);
        resolve(rows);
      });
    });
  }
};

export { User, Game };
