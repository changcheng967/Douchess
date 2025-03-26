const { Chess } = require('chess.js');
const { User, Game } = require('./models');

module.exports = function(io) {
  io.on('connection', (socket) => {
    console.log('New client connected');
    
    // Join game room
    socket.on('joinGame', async (gameId) => {
      socket.join(gameId);
      
      const game = await Game.findByPk(gameId, {
        include: [
          { model: User, as: 'whitePlayer' },
          { model: User, as: 'blackPlayer' }
        ]
      });
      
      if (game) {
        socket.emit('gameState', game);
      }
    });
    
    // Handle moves
    socket.on('move', async ({ gameId, move }) => {
      try {
        const gameDoc = await Game.findByPk(gameId);
        if (!gameDoc) return;
        
        const game = new Chess(gameDoc.fen);
        const result = game.move(move);
        
        if (result) {
          gameDoc.fen = game.fen();
          gameDoc.pgn = game.pgn();
          gameDoc.moves = [...gameDoc.moves, move];
          
          // Check for game over
          if (game.isGameOver()) {
            gameDoc.status = 'completed';
            
            if (game.isCheckmate()) {
              gameDoc.result = game.turn() === 'w' ? 'black' : 'white';
              
              // Update user stats
              const winnerId = gameDoc.result === 'white' ? 
                gameDoc.whitePlayerId : gameDoc.blackPlayerId;
              const loserId = gameDoc.result === 'white' ? 
                gameDoc.blackPlayerId : gameDoc.whitePlayerId;
              
              await User.updateStats(winnerId, 'win');
              await User.updateStats(loserId, 'loss');
            } else if (game.isDraw()) {
              gameDoc.result = 'draw';
              await User.updateStats(gameDoc.whitePlayerId, 'draw');
              await User.updateStats(gameDoc.blackPlayerId, 'draw');
            }
          }
          
          await gameDoc.save();
          io.to(gameId).emit('gameState', gameDoc);
        }
      } catch (err) {
        console.error(err);
      }
    });
    
    // Handle chat messages
    socket.on('chatMessage', ({ gameId, message, username }) => {
      io.to(gameId).emit('message', { 
        username, 
        message, 
        timestamp: new Date() 
      });
    });
    
    socket.on('disconnect', () => {
      console.log('Client disconnected');
    });
  });
};
