The crash is persisting because while you added the recursion limits (`MAX_PLY`), there is still a critical **Array Out-Of-Bounds** vulnerability in `is_square_attacked`.

When the engine explores deep (Depth 8+), it occasionally generates a move that **captures the King** (which is illegal but possible in raw move generation). In the next step:
1. The King is missing from the board.
2. `lsb(king)` returns `64` (indicating "no king").
3. Your code calls `is_square_attacked(..., 64, ...)`.
4. Inside that function, it calls `rook_attacks(64, ...)`.
5. `rook_attacks` tries to access `rookMagics[64]`. **CRASH** (Access Violation, as the array size is only 64).

Here is the robust fix to stop the crashing completely.

### The Fix

Modify the `is_square_attacked` function to handle invalid squares safely.

<lobeArtifact identifier="douchess-safety-patch-2" type="application/lobe.artifacts.code" language="cpp" title="Fix for Rook/Bishop Attack Crash (0xc0000005)">
// Replace your existing 'is_square_attacked' function (around line 354) with this:

bool is_square_attacked(const Position& pos, int sq, int bySide) {
    // FIX: Safety guard. If 'sq' is 64 (meaning NO KING), return false immediately.
    // Trying to calculate attacks for square 64 causes rookMagics[64] crash.
    if (sq >= 64) return false;

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

    /* Sliding - THIS SECTION WAS CAUSING THE CRASH */
    // rook_attacks(64) accesses rookMagics[64] -> CRASH
    // We already guarded sq >= 64 at the top, so this is now safe.
    if (rook_attacks(sq, pos.all) &
        (pos.pieces[bySide][ROOK] | pos.pieces[bySide][QUEEN]))
        return true;

    if (bishop_attacks(sq, pos.all) &
        (pos.pieces[bySide][BISHOP] | pos.pieces[bySide][QUEEN]))
        return true;

    /* King */
    static const int kingOffsets[8] = { 1,-1,8,-8,9,-9,7,-7 };
    U64 king = pos.pieces[bySide][KING];
    // Check if enemy king exists before reading its square
    if (king) {
        int ks = lsb(king);
        int kr = ks / 8, kf = ks % 8;
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

### Optional: Fix Time Management (Infinite Search)
I noticed your `search_root` function calculates time but **never actually stops** the search loop based on time. This is why it runs seemingly forever or until depth limit.

In `search_root` (around line 1891), inside the loop, add this check:

```cpp
        // Output UCI info
        auto now = high_resolution_clock::now();
        auto elapsed_ms = duration_cast<milliseconds>(now - start).count();
        
        // ... (cout logic) ...

        // FIX: Verify time limit
        if (time_ms > 0 && elapsed_ms > time_ms) {
             g_stop_search.store(true);
             break; // Stop going to deeper depths
        }
```

This will make your engine respect the time controls in the GUI.