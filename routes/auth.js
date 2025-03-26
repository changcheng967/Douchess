const express = require('express');
const router = express.Router();
const passport = require('passport');
const { User } = require('../models');
const bcrypt = require('bcryptjs');

// Register
router.get('/register', (req, res) => res.render('register'));
router.post('/register', async (req, res) => {
  try {
    const { username, password } = req.body;
    const hashedPassword = await bcrypt.hash(password, 10);
    
    await User.create({
      username,
      password: hashedPassword
    });
    
    res.redirect('/auth/login');
  } catch (error) {
    res.render('register', { error: error.message });
  }
});

// Login
router.get('/login', (req, res) => res.render('login'));
router.post('/login', passport.authenticate('local', {
  successRedirect: '/',
  failureRedirect: '/auth/login',
  failureFlash: false
}));

// Logout
router.get('/logout', (req, res) => {
  req.logout();
  res.redirect('/');
});

module.exports = router;
