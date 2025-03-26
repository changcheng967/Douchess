<<<<<<< HEAD
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
    console.log('Database synchronized');
  } catch (error) {
    console.error('Database error:', error);
  }
};

module.exports = { User, Game, initDb, Op };
=======
const mongoose = require('mongoose');
const bcrypt = require('bcryptjs');

const userSchema = new mongoose.Schema({
  username: { 
    type: String, 
    required: true, 
    unique: true,
    trim: true,
    minlength: 3,
    maxlength: 30
  },
  password: { 
    type: String, 
    required: true,
    minlength: 6
  },
  stats: {
    wins: { 
      type: Number, 
      default: 0,
      min: 0
    },
    losses: { 
      type: Number, 
      default: 0,
      min: 0
    },
    draws: { 
      type: Number, 
      default: 0,
      min: 0
    },
    rating: { 
      type: Number, 
      default: 1000,
      min: 0
    }
  },
  createdAt: { 
    type: Date, 
    default: Date.now 
  }
}, {
  toJSON: {
    transform: function(doc, ret) {
      delete ret.password;
      return ret;
    }
  }
});

userSchema.pre('save', async function(next) {
  if (!this.isModified('password')) return next();
  
  try {
    const salt = await bcrypt.genSalt(10);
    this.password = await bcrypt.hash(this.password, salt);
    next();
  } catch (err) {
    next(err);
  }
});

userSchema.methods.comparePassword = async function(candidatePassword) {
  return await bcrypt.compare(candidatePassword, this.password);
};

userSchema.methods.addWin = async function() {
  this.stats.wins += 1;
  this.stats.rating += 10;
  await this.save();
};

userSchema.methods.addLoss = async function() {
  this.stats.losses += 1;
  this.stats.rating = Math.max(0, this.stats.rating - 10);
  await this.save();
};

userSchema.methods.addDraw = async function() {
  this.stats.draws += 1;
  this.stats.rating += 5;
  await this.save();
};

const gameSchema = new mongoose.Schema({
  whitePlayer: { 
    type: mongoose.Schema.Types.ObjectId, 
    ref: 'User',
    required: true 
  },
  blackPlayer: { 
    type: mongoose.Schema.Types.ObjectId, 
    ref: 'User' 
  },
  fen: { 
    type: String, 
    default: 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1' 
  },
  pgn: { 
    type: String, 
    default: '' 
  },
  moves: [{ 
    type: String 
  }],
  status: { 
    type: String, 
    enum: ['waiting', 'active', 'completed'], 
    default: 'waiting' 
  },
  result: { 
    type: String, 
    enum: ['white', 'black', 'draw'] 
  },
  createdAt: { 
    type: Date, 
    default: Date.now 
  },
  completedAt: { 
    type: Date 
  }
}, {
  toJSON: { virtuals: true },
  toObject: { virtuals: true }
});

gameSchema.virtual('players', {
  ref: 'User',
  localField: '_id',
  foreignField: 'games'
});

gameSchema.pre('save', function(next) {
  if (this.isModified('result') && this.result) {
    this.status = 'completed';
    this.completedAt = new Date();
  }
  next();
});

const User = mongoose.model('User', userSchema);
const Game = mongoose.model('Game', gameSchema);

module.exports = { User, Game };
>>>>>>> Douchess-Init-0326
