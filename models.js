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
      delete ret.password; // Never return password in queries
      return ret;
    }
  }
});

// Password hashing middleware
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

// Method to compare passwords
userSchema.methods.comparePassword = async function(candidatePassword) {
  return await bcrypt.compare(candidatePassword, this.password);
};

// Stats management methods
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

// Virtual population for game history
gameSchema.virtual('players', {
  ref: 'User',
  localField: '_id',
  foreignField: 'games'
});

// Update game status when result is set
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
