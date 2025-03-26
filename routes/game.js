const express = require('express');
const router = express.Router();
const { Game, User } = require('../models');
const { ensureAuthenticated } = require('../middleware/auth');
const { Chess } = require('chess.js');

// Create new game
router.get('/create', ensureAuthenticated, async (req, res) => {
  try {
    const game = await Game.create({
      whitePlayerId: req.user.id,
      status: 'waiting'
    });
    
    res.redirect(`/game/${game.id}`);
  } catch (err) {
    console.error(err);
    req.flash('error_msg', 'Failed to create game');
    res.redirect('/');
  }
});

// Join game
router.get('/:id', ensureAuthenticated, async (req, res) => {
  try {
    const game = await Game.findByPk(req.params.id, {
      include: [
        { model: User, as: 'whitePlayer' },
        { model: User, as: 'blackPlayer' }
      ]
    });

    if (!game) {
      req.flash('error_msg', 'Game not found');
      return res.redirect('/');
    }

    // Join as black player if available
    if (!game.blackPlayerId && game.whitePlayerId !== req.user.id) {
      game.blackPlayerId = req.user.id;
      game.status = 'active';
      await game.save();
    }

    res.render('game', { 
      game,
      user: req.user,
      isWhite: game.whitePlayerId === req.user.id
    });
  } catch (err) {
    console.error(err);
    req.flash('error_msg', 'Failed to load game');
    res.redirect('/');
  }
});

// Make move (API endpoint)
router.post('/:id/move', ensureAuthenticated, async (req, res) => {
  try {
    const game = await Game.findByPk(req.params.id);
    if (!game) return res.status(404).json({ error: 'Game not found' });

    const chess = new Chess(game.fen);
    const move = chess.move(req.body.move);

    if (!move) return res.status(400).json({ error: 'Invalid move' });

    game.fen = chess.fen();
    game.pgn = chess.pgn();
    game.moves = [...game.moves, req.body.move];

    // Check game status
    if (chess.isGameOver()) {
      game.status = 'completed';
      if (chess.isCheckmate()) {
        game.result = chess.turn() === 'w' ? 'black' : 'white';
        
        // Update player stats
        const winnerId = game.result === 'white' ? 
          game.whitePlayerId : game.blackPlayerId;
        const loserId = game.result === 'white' ? 
          game.blackPlayerId : game.whitePlayerId;
        
        await User.incrementStats(winnerId, 'win');
        await User.incrementStats(loserId, 'loss');
      } else if (chess.isDraw()) {
        game.result = 'draw';
        await User.incrementStats(game.whitePlayerId, 'draw');
        await User.incrementStats(game.blackPlayerId, 'draw');
      }
    }

    await game.save();
    res.json({ 
      status: 'success',
      fen: game.fen,
      pgn: game.pgn
    });
  } catch (err) {
    console.error(err);
    res.status(500).json({ error: 'Server error' });
  }
});

module.exports = router;
