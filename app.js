document.addEventListener('DOMContentLoaded', () => {
    const boardElement = document.getElementById('chessboard');
    const game = new Chess();
    const board = Chessboard(boardElement, {
        draggable: true,
        position: 'start',
        onDragStart: (source, piece, position, orientation) => {
            if (game.in_checkmate() || game.in_draw() || piece.search(/^b/) !== -1) {
                return false;
            }
        },
        onDrop: (source, target) => {
            const move = game.move({
                from: source,
                to: target,
                promotion: 'q'
            });

            if (move === null) {
                return 'snapback';
            }
        },
        onMouseoutSquare: () => {
            boardEl.querySelectorAll('.square-55d63').forEach(square => {
                square.style.backgroundColor = '';
            });
        },
        onMouseoverSquare: (square) => {
            const moves = game.moves({
                square,
                verbose: true
            });

            if (moves.length === 0) return;

            moves.forEach(move => {
                document.querySelector(`.square-${move.to}`).style.backgroundColor = '#a9a9a9';
            });
        },
        onSnapEnd: () => {
            board.position(game.fen());
        }
    });
});
