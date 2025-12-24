// 1. Correct move_to_uci function
// Replace your existing implementation with this one to ensure GUI compatibility.
std::string move_to_uci(const Move& m) {
    // Handle null move
    if (m.from == m.to) return "0000";

    std::string s = "";
    s += (char)('a' + (m.from % 8));      // Source File
    s += (char)('1' + (m.from / 8));      // Source Rank
    s += (char)('a' + (m.to % 8));        // Dest File
    s += (char)('1' + (m.to / 8));        // Dest Rank

    // Handle Promotion
    if (m.promoted) {
        char pChar = ' ';
        switch(m.promoted) {
            case KNIGHT: pChar = 'n'; break;
            case BISHOP: pChar = 'b'; break;
            case ROOK:   pChar = 'r'; break;
            case QUEEN:  pChar = 'q'; break;
        }
        if (pChar != ' ') s += pChar;
    }
    return s;
}

// 2. Cleaned Search Function (Example)
// IMPORTANT: Remove all "std::cerr << DEBUG" lines from your intense recursive loops!

int search(Position& pos, int depth, int alpha, int beta, int ply, Undo* undoStack) {
    // 1. Max Depth / Time check
    if (depth <= 0) return quiescence(pos, alpha, beta, ply + 1);
    
    // 2. Repetition check (optional but recommended)
    
    // 3. Generate Moves
    Move moves[256];
    int moveCount = generate_moves(pos, moves);
    
    int legalMovesCount = 0;
    int bestScore = -1000000; // Better to use a defined -INFINITY constant

    for (int i = 0; i < moveCount; ++i) {
        Move m = moves[i];

        // Make move
        Undo u;
        // ... (save state)
        make_move(pos, m, u); 

        // Legality Check (King cannot be captured)
        int kingSq = lsb(pos.pieces[pos.side == WHITE ? BLACK : WHITE][KING]);
        if (is_square_attacked(pos, kingSq, pos.side)) {
            // ILLEGAL MOVE - Just Undo and Continue
            unmake_move(pos, m, u);
            continue; 
        }

        legalMovesCount++;

        // Recursive Search
        int score = -search(pos, depth - 1, -beta, -alpha, ply + 1, undoStack);

        unmake_move(pos, m, u);

        if (score >= beta) return beta; // fail-hard beta cutoff
        if (score > alpha) alpha = score;
    }

    // 4. Checkmate / Stalemate Detection
    if (legalMovesCount == 0) {
        int kingSq = lsb(pos.pieces[pos.side][KING]);
        if (is_square_attacked(pos, kingSq, pos.side ^ 1)) {
            return -100000 + ply; // Checkmate (prefer shorter mates)
        } else {
            return 0; // Stalemate
        }
    }

    return alpha;
}