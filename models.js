const { Sequelize, DataTypes } = require('sequelize');
const sequelize = require('./db');
const bcrypt = require('bcryptjs');

// User Model
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
});

// Game Model
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

// Associations
User.hasMany(Game, { as: 'whiteGames', foreignKey: 'whitePlayerId' });
User.hasMany(Game, { as: 'blackGames', foreignKey: 'blackPlayerId' });
Game.belongsTo(User, { as: 'whitePlayer', foreignKey: 'whitePlayerId' });
Game.belongsTo(User, { as: 'blackPlayer', foreignKey: 'blackPlayerId' });

// Initialize database
const initDb = async () => {
  try {
    await sequelize.authenticate();
    console.log('Connection to SQLite has been established successfully.');
    
    // Sync all models
    await sequelize.sync({ force: false }); // Set force: true to reset database
    console.log('All models were synchronized successfully.');
  } catch (error) {
    console.error('Unable to connect to the database:', error);
  }
};

module.exports = { User, Game, initDb };
