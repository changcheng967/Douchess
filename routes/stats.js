const express = require('express');
const router = express.Router();
const { User, Game } = require('../models');
const { Op } = require('sequelize');

router.get('/:username', async (req, res) => {
  try {
    const user = await User.findOne({ 
      where: { username: req.params.username } 
    });

    if (!user) {
      req.flash('error_msg', 'User not found');
      return res.redirect('/');
    }

    const games = await Game.findAll({
      where: {
        [Op.or]: [
          { whitePlayerId: user.id },
          { blackPlayerId: user.id }
        ],
        status: 'completed'
      },
      include: [
        { model: User, as: 'whitePlayer' },
        { model: User, as: 'blackPlayer' }
      ],
      order: [['updatedAt', 'DESC']],
      limit: 10
    });

    res.render('stats', {
      profile: user,
      games,
      currentUser: req.user
    });
  } catch (err) {
    console.error(err);
    req.flash('error_msg', 'Failed to load stats');
    res.redirect('/');
  }
});

module.exports = router;
