const { Sequelize, DataTypes, Op } = require('sequelize');
const sequelize = require('./db');
const bcrypt = require('bcryptjs');

const User = sequelize.define('User', {
  username: {
    type: DataTypes.STRING,
    allowNull: false,
    unique: true
  },
  password: {
    type: DataTypes.STRING,
    allowNull: false
  },
  stats: {
    type: DataTypes.JSON,
    defaultValue: {
      wins: 0,
      losses: 0,
      draws: 0,
      rating: 1000
    }
  }
}, {
  hooks: {
    beforeSave: async (user) => {
      if (user.changed('password')) {
        user.password = await bcrypt.hash(user.password, 10);
      }
    }
  }
});

const Game = sequelize.define('Game', {
  fen: {
    type: DataTypes.STRING,
    defaultValue: 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1'
  },
  pgn: {
    type: DataTypes.TEXT,
    defaultValue: ''
  },
  moves: {
    type: DataTypes.JSON,
    defaultValue: []
  },
  status: {
    type: DataTypes.ENUM('waiting', 'active', 'completed'),
    defaultValue: 'waiting'
  },
  result: {
    type: DataTypes.ENUM('white', 'black', 'draw'),
    allowNull: true
  }
});

// Model Methods
User.incrementStats = async function(userId, type) {
  const user = await User.findByPk(userId);
  if (!user) return;

  const stats = user.stats || { wins: 0, losses: 0, draws: 0, rating: 1000 };
  
  switch (type) {
    case 'win':
      stats.wins += 1;
      stats.rating += 10;
      break;
    case 'loss':
      stats.losses += 1;
      stats.rating = Math.max(0, stats.rating - 10);
      break;
    case 'draw':
      stats.draws += 1;
      stats.rating += 5;
      break;
  }

  await user.update({ stats });
};

// Associations
User.hasMany(Game, { as: 'whiteGames', foreignKey: 'whitePlayerId' });
User.hasMany(Game, { as: 'blackGames', foreignKey: 'blackPlayerId' });
Game.belongsTo(User, { as: 'whitePlayer', foreignKey: 'whitePlayerId' });
Game.belongsTo(User, { as: 'blackPlayer', foreignKey: 'blackPlayerId' });

const initDb = async () => {
  try {
    await sequelize.authenticate();
    await sequelize.sync({ force: false });
    console.log('Database connected and synced');
  } catch (error) {
    console.error('Database error:', error);
  }
};

module.exports = { User, Game, initDb, Op };
