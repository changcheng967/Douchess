document.addEventListener('DOMContentLoaded', () => {
    // DOM elements
    const homeView = document.getElementById('home-view');
    const gameView = document.getElementById('game-view');
    const createGameBtn = document.getElementById('create-game-btn');
    const gameLinkSection = document.getElementById('game-link-section');
    const gameLinkInput = document.getElementById('game-link');
    const copyLinkBtn = document.getElementById('copy-link-btn');
    const joinGameBtn = document.getElementById('join-game-btn');
    const gameCodeInput = document.getElementById('game-code-input');
    const checkStatsBtn = document.getElementById('check-stats-btn');
    const statsUsernameInput = document.getElementById('stats-username-input');
    const statsResult = document.getElementById('stats-result');
    const authModal = document.getElementById('auth-modal');
    const authSubmitBtn = document.getElementById('auth-submit-btn');
    const authSkipBtn = document.getElementById('auth-skip-btn');
  
    // Game elements
    const chessBoard = document.getElementById('chess-board');
    const gameCodeDisplay = document.getElementById('game-code-display');
    const playerColorDisplay = document.getElementById('player-color');
    const opponentNameDisplay = document.getElementById('opponent-name');
    const statusText = document.getElementById('status-text');
  
    // Game state
    let socket = null;
    let currentGame = null;
    let playerColor = null;
    let currentUser = { username: 'Anonymous' };
    let chess = new Chess();
    let selectedSquare = null;
  
    // Initialize chess board
    function renderBoard() {
      chessBoard.innerHTML = '';
      const board = chess.board();
      
      for (let row = 0; row < 8; row++) {
        for (let col = 0; col < 8; col++) {
          const square = document.createElement('div');
          square.className = `square ${(row + col) % 2 === 0 ? 'light' : 'dark'}`;
          square.dataset.row = row;
          square.dataset.col = col;
          
          const piece = board[row][col];
          if (piece) {
            const pieceElement = document.createElement('div');
            pieceElement.textContent = getPieceSymbol(piece);
            pieceElement.style.cursor = 'pointer';
            square.appendChild(pieceElement);
          }
          
          square.addEventListener('click', () => handleSquareClick(row, col));
          chessBoard.appendChild(square);
        }
      }
    }
  
    function getPieceSymbol(piece) {
      const symbols = {
        p: '♟', n: '♞', b: '♝', r: '♜', q: '♛', k: '♚',
        P: '♙', N: '♘', B: '♗', R: '♖', Q: '♕', K: '♔'
      };
      return symbols[piece.type + piece.color[0].toUpperCase()] || '';
    }
  
    function handleSquareClick(row, col) {
      if (!currentGame || chess.isGameOver() || 
          (chess.turn() === 'w' && playerColor !== 'white') ||
          (chess.turn() === 'b' && playerColor !== 'black')) return;
      
      const square = `${String.fromCharCode(97 + col)}${8 - row}`;
      
      if (selectedSquare) {
        // Try to make a move
        const move = {
          from: selectedSquare,
          to: square,
          promotion: 'q' // Always promote to queen for simplicity
        };
        
        try {
          socket.emit('makeMove', { move });
          selectedSquare = null;
          highlightSquare(null);
        } catch (error) {
          console.error('Invalid move:', error);
          selectedSquare = square;
          highlightSquare(square);
        }
      } else {
        // Select a piece
        const piece = chess.get(square);
        if (piece && piece.color === (playerColor === 'white' ? 'w' : 'b')) {
          selectedSquare = square;
          highlightSquare(square);
        }
      }
    }
  
    function highlightSquare(square) {
      document.querySelectorAll('.square').forEach(el => {
        el.classList.remove('highlight');
      });
      
      if (square) {
        const col = square.charCodeAt(0) - 97;
        const row = 8 - parseInt(square[1]);
        const index = row * 8 + col;
        document.querySelectorAll('.square')[index].classList.add('highlight');
      }
    }
  
    function updateGameView() {
      gameCodeDisplay.textContent = currentGame.short_code;
      playerColorDisplay.textContent = playerColor;
      opponentNameDisplay.textContent = playerColor === 'white' ? currentGame.black_name : currentGame.white_name;
    }
  
    // Event listeners
    createGameBtn.addEventListener('click', async () => {
      try {
        createGameBtn.disabled = true;
        createGameBtn.textContent = 'Creating...';
        
        const response = await fetch('/create-game', { method: 'POST' });
        const data = await response.json();
        
        if (response.ok) {
          gameLinkInput.value = `${window.location.origin}/#${data.shortCode}`;
          gameLinkSection.style.display = 'block';
        } else {
          alert('Error creating game: ' + (data.error || 'Unknown error'));
        }
      } catch (error) {
        alert('Error creating game: ' + error.message);
      } finally {
        createGameBtn.disabled = false;
        createGameBtn.textContent = 'Create New Game';
      }
    });
  
    copyLinkBtn.addEventListener('click', () => {
      gameLinkInput.select();
      document.execCommand('copy');
      alert('Link copied to clipboard!');
    });
  
    joinGameBtn.addEventListener('click', () => {
      const gameCode = gameCodeInput.value.trim();
      if (gameCode) {
        showAuthModal(gameCode);
      } else {
        alert('Please enter a game code');
      }
    });
  
    checkStatsBtn.addEventListener('click', async () => {
      const username = statsUsernameInput.value.trim();
      if (!username) {
        alert('Please enter a username');
        return;
      }
  
      try {
        checkStatsBtn.disabled = true;
        checkStatsBtn.textContent = 'Checking...';
        
        const response = await fetch(`/stats/${username}`);
        const data = await response.json();
        
        if (response.ok) {
          statsResult.innerHTML = `
            <h4>Stats for ${data.username}</h4>
            <p>Wins: ${data.wins || 0}</p>
            <p>Losses: ${data.losses || 0}</p>
            <p>Draws: ${data.draws || 0}</p>
            <p>ELO Rating: ${data.elo || 1000}</p>
          `;
        } else {
          statsResult.textContent = data.error || 'Error fetching stats';
        }
      } catch (error) {
        statsResult.textContent = 'Error fetching stats: ' + error.message;
      } finally {
        checkStatsBtn.disabled = false;
        checkStatsBtn.textContent = 'Check Stats';
      }
    });
  
    function showAuthModal(gameCode) {
      authModal.style.display = 'flex';
      
      authSubmitBtn.onclick = () => {
        const username = document.getElementById('auth-username').value.trim();
        const password = document.getElementById('auth-password').value;
        const isNew = document.getElementById('auth-is-new').checked;
        
        if (!username) {
          alert('Please enter a username');
          return;
        }
        
        joinGame(gameCode, username, password, isNew);
      };
      
      authSkipBtn.onclick = () => {
        joinGame(gameCode);
      };
    }
  
    function joinGame(gameCode, username = null, password = null, isNew = false) {
      authModal.style.display = 'none';
      
      if (!socket) {
        socket = io();
        setupSocketListeners();
      }
      
      socket.emit('joinGame', { 
        shortCode: gameCode,
        username,
        password,
        isNew
      }, (response) => {
        if (response.success) {
          currentGame = response.game;
          playerColor = response.color;
          if (response.user) {
            currentUser = response.user;
          }
          
          homeView.style.display = 'none';
          gameView.style.display = 'block';
          
          chess.load(currentGame.fen);
          renderBoard();
          updateGameView();
        } else {
          alert('Error joining game: ' + response.error);
        }
      });
    }
  
    function setupSocketListeners() {
      socket.on('gameUpdate', (game) => {
        currentGame = game;
        updateGameView();
        
        chess.load(game.fen);
        renderBoard();
        
        if (game.status === 'white_win') {
          statusText.textContent = game.white_name + ' wins!';
        } else if (game.status === 'black_win') {
          statusText.textContent = game.black_name + ' wins!';
        } else if (game.status === 'draw') {
          statusText.textContent = 'Game ended in a draw';
        } else {
          statusText.textContent = 'Current turn: ' + (chess.turn() === 'w' ? game.white_name : game.black_name);
        }
      });
  
      socket.on('moveError', (error) => {
        alert('Move error: ' + error);
      });
    }
  
    // Handle game link from URL hash
    if (window.location.hash) {
      const gameCode = window.location.hash.substring(1);
      gameCodeInput.value = gameCode;
      showAuthModal(gameCode);
    }
  
    // Initial render
    renderBoard();
  });