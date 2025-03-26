require('dotenv').config();
const express = require('express');
const session = require('express-session');
const passport = require('passport');
const LocalStrategy = require('passport-local').Strategy;
const bcrypt = require('bcryptjs');
const socketio = require('socket.io');
const { Chess } = require('chess.js');
const { User, Game, initDb } = require('./models');
const path = require('path');

const app = express();

// Initialize database
initDb();

// Middleware
app.use(express.static(path.join(__dirname, 'public')));
app.use(express.urlencoded({ extended: true }));
app.use(express.json());
app.set('view engine', 'ejs');
app.set('views', path.join(__dirname, 'views'));

// Session configuration
app.use(session({
  secret: process.env.SESSION_SECRET || 'your-secret-key',
  resave: false,
  saveUninitialized: false,
  cookie: { secure: false } // Set to true if using HTTPS
}));

// Passport initialization
app.use(passport.initialize());
app.use(passport.session());

// Passport configuration
passport.use(new LocalStrategy(async (username, password, done) => {
  try {
    const user = await User.findOne({ where: { username } });
    if (!user) return done(null, false, { message: 'Incorrect username.' });
    
    const isMatch = await bcrypt.compare(password, user.password);
    if (!isMatch) return done(null, false, { message: 'Incorrect password.' });
    
    return done(null, user);
  } catch (err) {
    return done(err);
  }
}));

passport.serializeUser((user, done) => done(null, user.id));
passport.deserializeUser(async (id, done) => {
  try {
    const user = await User.findByPk(id);
    done(null, user);
  } catch (err) {
    done(err);
  }
});

// Routes
app.get('/', async (req, res) => {
  const games = await Game.findAll({
    include: [
      { model: User, as: 'whitePlayer' },
      { model: User, as: 'blackPlayer' }
    ]
  });
  res.render('index', { user: req.user, games });
});

// Auth routes
app.get('/register', (req, res) => res.render('register'));
app.post('/register', async (req, res) => {
  try {
    const { username, password } = req.body;
    const hashedPassword = await bcrypt.hash(password, 10);
    const user = await User.create({ 
      username, 
      password: hashedPassword,
      stats: {
        wins: 0,
        losses: 0,
        draws: 0,
        rating: 1000
      }
    });
    res.redirect('/login');
  } catch (err) {
    res.render('register', { error: err.message });
  }
});

app.get('/login', (req, res) => res.render('login'));
app.post('/login', passport.authenticate('local', {
  successRedirect: '/',
  failureRedirect: '/login',
  failureFlash: false
}));

app.get('/logout', (req, res) => {
  req.logout();
  res.redirect('/');
});

// Game routes
app.get('/game/create', async (req, res) => {
  if (!req.user) return res.redirect('/login');
  
  const game = await Game.create({
    whitePlayerId: req.user.id,
    fen: 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1',
    pgn: '',
    status: 'waiting'
  });
  
  res.redirect(`/game/${game.id}`);
});

app.get('/game/:id', async (req, res) => {
  if (!req.user) return res.redirect('/login');
  
  const game = await Game.findByPk(req.params.id, {
    include: [
      { model: User, as: 'whitePlayer' },
      { model: User, as: 'blackPlayer' }
    ]
  });
  
  if (!game) return res.status(404).send('Game not found');
  
  // Join game as black player if available
  if (!game.blackPlayerId && game.whitePlayerId !== req.user.id) {
    game.blackPlayerId = req.user.id;
    game.status = 'active';
    await game.save();
  }
  
  res.render('game', { game, user: req.user });
});

// Stats route
app.get('/stats/:username', async (req, res) => {
  const user = await User.findOne({ 
    where: { username: req.params.username } 
  });
  
  if (!user) return res.status(404).send('User not found');
  
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
    ]
  });
  
  res.render('stats', { profile: user, games, currentUser: req.user });
});

// Start server
const PORT = process.env.PORT || 3000;
const server = app.listen(PORT, () => {
  console.log(`Server running on port ${PORT}`);
});

// Socket.io setup
const io = socketio(server);
require('./sockets')(io);
