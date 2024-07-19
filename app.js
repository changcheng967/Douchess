document.addEventListener('DOMContentLoaded', () => {
    const boardElement = document.getElementById('chessboard');
    const game = new Chess();
    const statusElement = document.getElementById('status-text');
    const movesElement = document.getElementById('moves');

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

            updateStatus();
            updateMoveHistory(move);
        },
        onMouseoutSquare: () => {
            boardElement.querySelectorAll('.square-55d63').forEach(square => {
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

    function updateStatus() {
        let status = '';

        const moveColor = game.turn() === 'b' ? 'Black' : 'White';

        if (game.in_checkmate()) {
            status = `Game over, ${moveColor} is in checkmate.`;
        } else if (game.in_draw()) {
            status = 'Game over, drawn position';
        } else {
            status = `${moveColor} to move`;

            if (game.in_check()) {
                status += `, ${moveColor} is in check`;
            }
        }

        statusElement.textContent = status;
    }

    function updateMoveHistory(move) {
        const moveElement = document.createElement('li');
        moveElement.textContent = move.san;
        movesElement.appendChild(moveElement);
    }

    updateStatus();
});
