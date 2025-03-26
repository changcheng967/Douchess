const express = require('express');
const router = express.Router();
const { Game } = require('../models');

router.get('/', async (req, res) => {
  try {
    console.log("Fetching games...");
    const games = await Game.findAll({
      include: ['whitePlayer', 'blackPlayer'],
      limit: 10,
      order: [['createdAt', 'DESC']]
    });
    
    console.log("Games found:", games.length);
    res.render('index', { 
      user: req.user,
      games: games || []
    });
  } catch (err) {
    console.error("Error fetching games:", err);
    res.render('index', { 
      user: req.user,
      games: [],
      error: 'Failed to load games'
    });
  }
});

module.exports = router;
