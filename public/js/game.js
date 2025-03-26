function initGame(gameId, username, isWhite) {
  const socket = io();
  const messageInput = document.getElementById('messageInput');
  const sendMessageBtn = document.getElementById('sendMessage');
  const messagesDiv = document.getElementById('messages');
  
  // Initialize chessboard
  const board = Chessboard('board', {
    position: 'start',
    draggable: true,
    onDrop: handleMove
  });
  
  // Join game room
  socket.emit('joinGame', gameId);
  
  // Handle game state updates
  socket.on('gameState', (game) => {
    board.position(game.fen);
    updateGameInfo(game);
  });
  
  // Handle chat messages
  socket.on('message', (msg) => {
    const messageElement = document.createElement('div');
    messageElement.innerHTML = `<strong>${msg.username}:</strong> ${msg.message}`;
    messagesDiv.appendChild(messageElement);
    messagesDiv.scrollTop = messagesDiv.scrollHeight;
  });
  
  // Send message
  sendMessageBtn.addEventListener('click', sendMessage);
  messageInput.addEventListener('keypress', (e) => {
    if (e.key === 'Enter') sendMessage();
  });
  
  function sendMessage() {
    const message = messageInput.value.trim();
    if (message) {
      socket.emit('chatMessage', { gameId, message, username });
      messageInput.value = '';
    }
  }
  
  function handleMove(source, target) {
    const move = {
      from: source,
      to: target,
      promotion: 'q' // always promote to queen for simplicity
    };
    
    socket.emit('move', { gameId, move });
  }
  
  function updateGameInfo(game) {
    // Update game status, moves list, etc.
    // Implementation depends on your specific UI needs
  }
}
