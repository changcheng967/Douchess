const { User, Game } = require('./models');
const Chess = require('chess.js').Chess;

function setupSockets(io) {
  io.on('connection', (socket) => {
    console.log('New connection:', socket.id);

    let currentGame = null;
    let currentUser = null;

    socket.on('joinGame', async ({ shortCode, username, password, isNew }, callback) => {
      try {
        // Authenticate
        let authResponse;
        if (username) {
          authResponse = await authenticateUser(username, password, isNew);
          if (authResponse.error) {
            return callback({ error: authResponse.error });
          }
          currentUser = authResponse.user;
        } else {
          currentUser = { username: 'Anonymous' };
        }

        // Find game
        const game = await Game.getByCode(shortCode);
        if (!game) {
          return callback({ error: 'Game not found' });
        }

        // Determine color
        let color;
        if (!game.white_id && !game.black_id) {
          color = Math.random() > 0.5 ? 'white' : 'black';
        } else if (!game.white_id) {
          color = 'white';
        } else if (!game.black_id) {
          color = 'black';
        } else {
          return callback({ error: 'Game is full' });
        }

        // Join game
        await Game.join(game.id, currentUser.id, currentUser.username, color);
        currentGame = await Game.get(game.id);

        socket.join(`game_${game.id}`);
        callback({ 
          success: true, 
          game: currentGame,
          color,
          user: currentUser
        });

        io.to(`game_${game.id}`).emit('gameUpdate', currentGame);
      } catch (error) {
        console.error('Join game error:', error);
        callback({ error: error.message });
      }
    });

    socket.on('makeMove', async ({ move }) => {
      if (!currentGame) return;

      try {
        const chess = new Chess(currentGame.fen);
        chess.move(move);

        const status = await Game.makeMove(currentGame.id, chess.fen(), chess.pgn());
        const updatedGame = await Game.get(currentGame.id);
        currentGame = updatedGame;

        io.to(`game_${currentGame.id}`).emit('gameUpdate', updatedGame);

        if (status !== 'active') {
          const winner = status === 'white_win' ? updatedGame.white_id : 
                         status === 'black_win' ? updatedGame.black_id : null;
          
          if (currentUser.id) {
            if (winner === currentUser.id) {
              await User.updateStats(currentUser.id, 'win');
            } else if (winner !== null) {
              await User.updateStats(currentUser.id, 'loss');
            } else {
              await User.updateStats(currentUser.id, 'draw');
            }
          }
        }
      } catch (error) {
        socket.emit('moveError', error.message);
      }
    });

    socket.on('disconnect', () => {
      console.log('Disconnected:', socket.id);
    });
  });
}

async function authenticateUser(username, password, isNew) {
  try {
    let user;
    if (isNew) {
      const existing = await User.findByName(username);
      if (existing) {
        return { error: 'Username already exists' };
      }
      const userId = await User.create(username, password);
      user = { id: userId, username };
    } else {
      user = await User.findByName(username);
      if (!user || !(await User.comparePassword(password, user.password))) {
        return { error: 'Invalid credentials' };
      }
    }
    return { user };
  } catch (error) {
    return { error: error.message };
  }
}

module.exports = setupSockets;