const { Sequelize } = require('sequelize');
const path = require('path');

const sequelize = new Sequelize({
  dialect: 'sqlite',
  storage: path.join(__dirname, 'chess-platform.db'),
  logging: false
});

module.exports = sequelize;
