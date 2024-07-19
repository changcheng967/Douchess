document.addEventListener('DOMContentLoaded', () => {
    const chessBoard = document.getElementById('chess-board');
    
    // Sample grid for chess board
    for (let i = 0; i < 8; i++) {
        for (let j = 0; j < 8; j++) {
            const square = document.createElement('div');
            square.style.width = '50px';
            square.style.height = '50px';
            square.style.display = 'inline-block';
            square.style.backgroundColor = (i + j) % 2 === 0 ? '#fff' : '#000';
            chessBoard.appendChild(square);
        }
    }
});
