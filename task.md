Based on the deep analysis of your updated source code and the error logs provided (`ERROR: No king for side 1`), here is the diagnosis and the required fixes.

### The Diagnosis
1.  **The "No King" Error:** Your engine is currently generating and executing **Illegal Moves that capture the Opponent's King**.
    *   In Chess, you cannot capture the King. The game ends (Checkmate) before that happens.
    *   Currently, your `generate_moves` function treats the King as a regular piece that can be captured.
    *   When the engine explores a line where it captures the King, the board state becomes corrupted (King removed), leading to `ERROR: No king for side...` when the search tries to continue.

2.  **The Crash Risk (Access Violation):**
    *   When the King is missing (due to the bug above), accessing `lsb(king)` returns `64`.
    *   Your code then calls `is_square_attacked(..., 64, ...)`.
    *   Inside that function, `rook_attacks(64)` accesses `rookMagics[64]`, which is **out of bounds** (valid indices are 0-63). This causes the `0xc0000005` crash.

3.  **Observation:** The engine reached depth 30 (`info depth 30`) but is "spamming" errors. This means the engine is stable enough to run but is wasting calculations on illegal lines and printing errors to the console, likely degrading performance and correctness.

### The Fix
You need to replace two functions: `generate_moves` (to stop generating King captures) and `is_square_attacked` (to handle invalid squares safely).

#### 1. Update `is_square_attacked`
This adds a safety guard to prevent crashes if the engine ever tries to check a missing King.

<lobeArtifact identifier="douchess-safety-update" type="application/lobe.artifacts.code" language="cpp" title="Safety Update for douchess.cpp">
// REPLACE your is_square_attacked function with this version that has Bounds Checking
bool is_square_attacked(const Position& pos, int sq, int bySide) {
    // [FIX] CRITICAL SAFETY GUARD: 
    // If we passed 64 (meaning NO KING / INVALID SQUARE), return false immediately.
    // This prevents the arrays rookMagics[64] from being accessed, which causes the crash.
    if (sq < 0 || sq >= 64) return false;

    U64 attackers = 0;

    /* Pawns */
    if (bySide == WHITE) {
        attackers |= (pos.pieces[WHITE][PAWN] >> 7) & notHFile;
        attackers |= (pos.pieces[WHITE][PAWN] >> 9) & notAFile;
    }
    else {
        attackers |= (pos.pieces[BLACK][PAWN] << 7) & notAFile;
        attackers |= (pos.pieces[BLACK][PAWN] << 9) & notHFile;
    }
    if (attackers & bit(sq)) return true;

    /* Knights */
    static const int knightOffsets[8] = { 17,15,10,6,-6,-10,-15,-17 };
    U64 knights = pos.pieces[bySide][KNIGHT];
    while (knights) {
        int s = poplsb(knights);
        int r = s / 8, f = s % 8;
        for (int o : knightOffsets) {
            int t = s + o;
            if (t < 0 || t >= 64) continue;
            int tr = t / 8, tf = t % 8;
            if (abs(tr - r) > 2 || abs(tf - f) > 2) continue;
            if (t == sq) return true;
        }
    }

    /* Sliding Pieces - This was the crash source */
    if (rook_attacks(sq, pos.all) &
        (pos.pieces[bySide][ROOK] | pos.pieces[bySide][QUEEN]))
        return true;

    if (bishop_attacks(sq, pos.all) &
        (pos.pieces[bySide][BISHOP] | pos.pieces[bySide][QUEEN]))
        return true;

    /* King */
    U64 king = pos.pieces[bySide][KING];
    if (king) { // Ensure king exists
        int ks = lsb(king);
        int kr = ks / 8, kf = ks % 8;
        static const int kingOffsets[8] = { 1,-1,8,-8,9,-9,7,-7 };
        for (int o : kingOffsets) {
            int t = ks + o;
            if (t < 0 || t >= 64) continue;
            int tr = t / 8, tf = t % 8;
            if (abs(tr - kr) > 1 || abs(tf - kf) > 1) continue;
            if (t == sq) return true;
        }
    }

    return false;
}
</lobeArtifact>

#### 2. Update `generate_moves`
This logic ensures we never generate a move matching the opponent's King square, stopping the "Error: No King" messages at the source.

```cpp
// REPLACE your generate_moves function with this strict version
void generate_moves(Position& pos, std::vector<Move>& moves) {
    moves.clear();
    int us = pos.side;
    int them = us ^ 1;
    U64 own = pos.occ[us];
    U64 all = pos.all;
    
    // [FIX] Define enemies valid for capture (Exclude Opponent King)
    U64 enemy_king_bb = pos.pieces[them][KING];
    U64 valid_targets = ~own & ~enemy_king_bb; // Target any square EXCEPT own pieces and enemy king
    U64 capture_targets = pos.occ[them] & ~enemy_king_bb; // Specifically pieces we can capture

    /* ===================
       PAWNS
    =================== */
    U64 pawns = pos.pieces[us][PAWN];
    int forward = (us == WHITE) ? 8 : -8;
    int startRank = (us == WHITE) ? 1 : 6;
    int promoRank = (us == WHITE) ? 6 : 1;

    while (pawns) {
        int sq = poplsb(pawns);
        int r = sq / 8;
        int f = sq % 8;

        int one = sq + forward;
        // Single push (quiet)
        if (one >= 0 && one < 64 && !(all & bit(one))) {
            if (r == promoRank) {
                add_move(moves, sq, one, QUEEN);
                add_move(moves, sq, one, ROOK);
                add_move(moves, sq, one, BISHOP);
                add_move(moves, sq, one, KNIGHT);
            } else {
                add_move(moves, sq, one);
            }
        }
        // Double push (quiet)
        if (r == startRank) {
            int two = sq + 2 * forward;
            int one_sq = sq + forward;
            if (two >= 0 && two < 64 && !(all & bit(one_sq)) && !(all & bit(two))) {
                add_move(moves, sq, two);
            }
        }

        // Captures (Must check strict capture_targets)
        int capL = sq + forward - 1;
        int capR = sq + forward + 1;
        
        if (f > 0 && capL >= 0 && capL < 64 && (capture_targets & bit(capL))) {
            if (r == promoRank) {
                add_move(moves, sq, capL, QUEEN); add_move(moves, sq, capL, ROOK);
                add_move(moves, sq, capL, BISHOP); add_move(moves, sq, capL, KNIGHT);
            } else add_move(moves, sq, capL);
        }
        if (f < 7 && capR >= 0 && capR < 64 && (capture_targets & bit(capR))) {
            if (r == promoRank) {
                add_move(moves, sq, capR, QUEEN); add_move(moves, sq, capR, ROOK);
                add_move(moves, sq, capR, BISHOP); add_move(moves, sq, capR, KNIGHT);
            } else add_move(moves, sq, capR);
        }

        // En passant
        if (pos.ep != -1) {
            int ep_rank = (us == WHITE) ? 4 : 3;
            if (r == ep_rank) {
                if (f > 0 && sq + forward - 1 == pos.ep) {
                    int ep_pawn = pos.ep + ((us == WHITE) ? -8 : 8);
                    if (pos.pieces[them][PAWN] & bit(ep_pawn)) add_move(moves, sq, pos.ep);
                }
                if (f < 7 && sq + forward + 1 == pos.ep) {
                    int ep_pawn = pos.ep + ((us == WHITE) ? -8 : 8);
                    if (pos.pieces[them][PAWN] & bit(ep_pawn)) add_move(moves, sq, pos.ep);
                }
            }
        }
    }

    /* ===================
       KNIGHT
    =================== */
    U64 knights = pos.pieces[us][KNIGHT];
    while (knights) {
        int sq = poplsb(knights);
        // [FIX] Apply valid_targets mask (excludes King capture)
        U64 moveset = knight_moves[sq] & valid_targets;
        while (moveset) add_move(moves, sq, poplsb(moveset));
    }

    /* ===================
       SLIDING PIECES
    =================== */
    U64 bishops = pos.pieces[us][BISHOP];
    while (bishops) {
        int sq = poplsb(bishops);
        // [FIX] Apply valid_targets mask
        U64 at = bishop_attacks(sq, all) & valid_targets;
        while (at) add_move(moves, sq, poplsb(at));
    }

    U64 rooks = pos.pieces[us][ROOK];
    while (rooks) {
        int sq = poplsb(rooks);
        // [FIX] Apply valid_targets mask
        U64 at = rook_attacks(sq, all) & valid_targets;
        while (at) add_move(moves, sq, poplsb(at));
    }

    U64 queens = pos.pieces[us][QUEEN];
    while (queens) {
        int sq = poplsb(queens);
        // [FIX] Apply valid_targets mask
        U64 at = (rook_attacks(sq, all) | bishop_attacks(sq, all)) & valid_targets;
        while (at) add_move(moves, sq, poplsb(at));
    }

    /* ===================
        KING
    =================== */
    U64 king_bb = pos.pieces[us][KING];
    if (king_bb) {
        int ks = lsb(king_bb);
        // [FIX] Apply valid_targets mask (King can't capture King anyway, but safe practice)
        U64 kmoves = king_moves[ks] & valid_targets;
        while (kmoves) add_move(moves, ks, poplsb(kmoves));
    }

    /* ===================
       CASTLING
    =================== */
    if (us == WHITE) {
        if ((pos.castling & 1) && !(all & 0x60ULL) &&
            !is_square_attacked(pos, 4, BLACK) && !is_square_attacked(pos, 5, BLACK) && !is_square_attacked(pos, 6, BLACK))
            add_move(moves, 4, 6);
        if ((pos.castling & 2) && !(all & 0x0EULL) &&
            !is_square_attacked(pos, 4, BLACK) && !is_square_attacked(pos, 3, BLACK) && !is_square_attacked(pos, 2, BLACK))
            add_move(moves, 4, 2);
    }
    else {
        if ((pos.castling & 4) && !(all & (0x60ULL << 56)) &&
            !is_square_attacked(pos, 60, WHITE) && !is_square_attacked(pos, 61, WHITE) && !is_square_attacked(pos, 62, WHITE))
            add_move(moves, 60, 62);
        if ((pos.castling & 8) && !(all & (0x0EULL << 56)) &&
            !is_square_attacked(pos, 60, WHITE) && !is_square_attacked(pos, 59, WHITE) && !is_square_attacked(pos, 58, WHITE))
            add_move(moves, 60, 58);
    }
}
```