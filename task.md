# Complete Action Plan: Building a High-ELO Chess Engine (1 Second/Move, No Books)

Based on your goal of achieving **maximum ELO with 1-second moves, no training, and no opening/endgame books**, here's your comprehensive fix list:

---

## **PHASE 1: CRITICAL FIXES (Must Fix - Engine Breaking)** 🔴

### **Correctness Issues**
1. ✅ **Fix Passed Pawn Detection** (Lines 1150-1200)
   - Current logic checks wrong ranks
   - Rewrite to properly detect passed pawns for both colors
   - Add rank-based bonus scaling (7th rank = 50cp, 6th = 30cp, etc.)

2. ✅ **Fix SEE (Static Exchange Evaluation)** (Lines 2050-2100)
   - Current implementation only checks one recapture
   - Implement full swap algorithm with all attackers/defenders
   - Use for capture ordering and pruning decisions

3. ✅ **Fix Array Bounds in `score_move()`** (Lines 1950-1960)
   - Add validation: `if (curr_piece < 0 || curr_piece >= 6) return 0;`
   - Prevent crashes from invalid piece types

4. ✅ **Remove Duplicate PST Tables** (Lines 350-550)
   - Delete unused `pst_midgame` and `pst_endgame` arrays
   - Keep only `pesto_mg` and `pesto_eg`

5. ✅ **Fix King Safety Check** (Line 850)
   - Add explicit check: `if (!king) return false;` before using king square

---

## **PHASE 2: SEARCH IMPROVEMENTS (Major ELO Gains)** 🟠

### **Search Enhancements (+200-300 ELO)**
6. ✅ **Implement Aspiration Windows** (Line 2800 in `search_root()`)
   ```cpp
   int window = 50;
   int alpha = previous_score - window;
   int beta = previous_score + window;
   // Re-search with wider window if fail-high/low
   ```

7. ✅ **Improve LMR Formula** (Lines 2650-2660)
   ```cpp
   // Use logarithmic reduction
   int R = (int)(0.75 + log(depth) * log(i) / 2.25);
   if (!is_pv_node) R++;
   if (is_capture) R--;
   ```

8. ✅ **Add Singular Extensions** (After line 2500)
   ```cpp
   // If TT move is much better than alternatives, extend search
   if (tt_hit && depth >= 8 && tt_move is valid) {
       // Search other moves at reduced depth
       // If none beat (tt_score - margin), extend tt_move
   }
   ```

9. ✅ **Add Razoring** (After line 2400)
   ```cpp
   if (!in_check && depth <= 3 && eval + 300 < alpha) {
       int razor_score = quiescence(pos, alpha, beta, ply);
       if (razor_score < alpha) return razor_score;
   }
   ```

10. ✅ **Add Multi-Cut Pruning** (In move loop)
    ```cpp
    int cutoff_count = 0;
    if (depth >= 6 && !in_check) {
        // If 3+ moves cause beta cutoff at depth-3, prune node
    }
    ```

11. ✅ **Add Probcut** (Before move generation)
    ```cpp
    if (depth >= 5 && abs(beta) < 20000) {
        int probcut_beta = beta + 200;
        // Search captures at reduced depth
        // If score >= probcut_beta, return beta
    }
    ```

12. ✅ **Improve Null Move Pruning** (Line 2450)
    ```cpp
    // Add verification search in zugzwang-prone positions
    if (non_pawn_material < 1300) { // Endgame
        // Do verification search instead of trusting null move
    }
    ```

---

## **PHASE 3: EVALUATION IMPROVEMENTS (+100-150 ELO)** 🟡

### **Positional Understanding**
13. ✅ **Fix Phase Calculation** (Line 1720)
    ```cpp
    // Use standard formula
    int phase = 24; // Total phase
    phase -= __popcnt64(pos.pieces[WHITE][KNIGHT] | pos.pieces[BLACK][KNIGHT]);
    phase -= __popcnt64(pos.pieces[WHITE][BISHOP] | pos.pieces[BLACK][BISHOP]);
    phase -= 2 * __popcnt64(pos.pieces[WHITE][ROOK] | pos.pieces[BLACK][ROOK]);
    phase -= 4 * __popcnt64(pos.pieces[WHITE][QUEEN] | pos.pieces[BLACK][QUEEN]);
    phase = (phase * 256 + 12) / 24; // Scale to 0-256
    ```

14. ✅ **Optimize Mobility Calculation** (Lines 1250-1320)
    ```cpp
    // Replace manual loops with __popcnt64()
    mobility += __popcnt64(knight_moves[sq] & ~own) * 2;
    ```

15. ✅ **Add Rook on 7th Rank Bonus** (In `evaluate()`)
    ```cpp
    if (rank == 6 && enemy_king_rank >= 6) score += 30; // White
    if (rank == 1 && enemy_king_rank <= 1) score += 30; // Black
    ```

16. ✅ **Precompute File Masks** (Line 1100)
    ```cpp
    // Move file masks to global constants
    constexpr U64 FILE_MASKS[8] = {
        0x0101010101010101ULL, 0x0202020202020202ULL, ...
    };
    ```

17. ✅ **Add Connected Rooks Bonus** (In `evaluate()`)
    ```cpp
    if (__popcnt64(rooks) >= 2) {
        // Check if rooks are on same rank/file
        score += 20;
    }
    ```

18. ✅ **Add King Tropism** (Improve king safety)
    ```cpp
    // Penalize enemy pieces near our king based on distance
    int distance = max(abs(king_rank - piece_rank), abs(king_file - piece_file));
    score -= (8 - distance) * piece_weight;
    ```

---

## **PHASE 4: TIME MANAGEMENT (+50 ELO)** ⏱️

19. ✅ **Implement Smart Time Allocation** (Line 2850)
    ```cpp
    // Allocate more time for critical positions
    int base_time = mytime / 30;
    if (score_drop > 50) base_time *= 1.5; // Position worsening
    if (best_move_stable) base_time *= 0.8; // Same move for 3+ depths
    time_ms = min(base_time + myinc, mytime / 10); // Hard limit
    ```

20. ✅ **Add Soft/Hard Time Limits** (In `search_root()`)
    ```cpp
    int soft_limit = time_ms * 0.6; // Stop if best move stable
    int hard_limit = time_ms * 0.95; // Absolute stop
    ```

21. ✅ **Implement Move Stability Check** (In iterative deepening)
    ```cpp
    if (best_move == previous_best_move) {
        stability_counter++;
        if (stability_counter >= 3 && elapsed > soft_limit) break;
    }
    ```

---

## **PHASE 5: MOVE ORDERING (+80-100 ELO)** 📊

22. ✅ **Increase Killer Slots** (Line 1850)
    ```cpp
    Move killers[100][3]; // 3 killers per ply instead of 2
    ```

23. ✅ **Add Counter-Move History** (Improve history heuristic)
    ```cpp
    int counter_move_history[64][64][64][64]; // [prev_from][prev_to][from][to]
    ```

24. ✅ **Implement History Gravity** (Prevent history overflow)
    ```cpp
    // Age history scores periodically
    if (search_count % 1000 == 0) {
        for (auto& h : history) h /= 2;
    }
    ```

25. ✅ **Add Capture History** (Separate from quiet history)
    ```cpp
    int capture_history[2][6][64][6]; // [side][piece][to][captured_piece]
    ```

---

## **PHASE 6: UCI COMPLIANCE & STABILITY** 🔧

26. ✅ **Add UCI Options** (Line 3100)
    ```cpp
    else if (token == "setoption") {
        // Support: Hash, Threads, Contempt, Move Overhead
    }
    ```

27. ✅ **Implement Hash Size Control**
    ```cpp
    void resize_tt(int mb) {
        // Dynamically allocate TT based on UCI Hash option
    }
    ```

28. ✅ **Add Move Overhead** (For network lag)
    ```cpp
    int move_overhead = 50; // UCI option
    time_ms -= move_overhead; // Subtract from allocated time
    ```

29. ✅ **Fix `ucinewgame` Handling** (Line 3120)
    - Already implemented, but verify TT clearing works correctly

30. ✅ **Add `info` Output** (In `search_root()`)
    ```cpp
    // Output: depth, seldepth, score, nodes, nps, hashfull, tbhits, time, pv
    std::cout << "info depth " << d 
              << " seldepth " << max_ply_reached
              << " score cp " << score
              << " nodes " << nodes
              << " nps " << (nodes * 1000 / elapsed)
              << " hashfull " << (tt_used * 1000 / TT_SIZE)
              << " time " << elapsed
              << " pv " << pv_line << std::endl;
    ```

---

## **PHASE 7: ENDGAME KNOWLEDGE (+50-80 ELO)** 🏁

31. ✅ **Add Basic Endgame Evaluation**
    ```cpp
    // KPK: Drive enemy king away from pawn
    // KBN vs K: Drive king to correct corner
    // KQ vs K: Checkmate pattern
    ```

32. ✅ **Implement Insufficient Material Detection**
    ```cpp
    bool is_insufficient_material(const Position& pos) {
        // K vs K, KB vs K, KN vs K, KB vs KB (same color)
        return true; // Return draw
    }
    ```

33. ✅ **Add Pawn Endgame Heuristics**
    ```cpp
    // King activity in pawn endgames
    // Opposition detection
    // Square rule for passed pawns
    ```

---

## **PHASE 8: OPTIMIZATION (+20-30 ELO from Speed)** ⚡

34. ✅ **Remove Dead Code**
    - Delete `evaluate_tuned()` (line 2200)
    - Delete unused global variables (lines 640-650)
    - Delete `test_fixes()` and `test_perft_all()`

35. ✅ **Optimize Bitboard Operations**
    ```cpp
    // Use PEXT/PDEP if available (BMI2)
    #ifdef __BMI2__
        // Use _pext_u64 for magic bitboards
    #endif
    ```

36. ✅ **Add Prefetching** (For TT access)
    ```cpp
    __builtin_prefetch(&TT[key & (TT_SIZE - 1)]);
    ```

37. ✅ **Optimize `make_move()`/`unmake_move()`**
    - Remove assertions in release builds
    - Use incremental zobrist updates

---

## **PHASE 9: TESTING & TUNING** 🧪

38. ✅ **Add Perft Verification**
    ```cpp
    // Verify move generation correctness
    // Test positions: startpos, kiwipete, position 3-6
    ```

39. ✅ **Implement Self-Play Testing**
    ```cpp
    // Play engine vs itself at different depths
    // Verify no crashes, illegal moves, or time losses
    ```

40. ✅ **Tune Evaluation Parameters**
    ```cpp
    // Use Texel tuning on quiet positions
    // Optimize piece values, PST, mobility weights
    ```

41. ✅ **Tune Search Parameters**
    ```cpp
    // LMR reduction amounts
    // Null move R value
    // Futility margins
    // Use SPSA or genetic algorithms
    ```

---

## **PHASE 10: FINAL POLISH** ✨

42. ✅ **Add Logging** (For debugging)
    ```cpp
    #ifdef DEBUG
        std::ofstream log("engine.log");
        log << "Search depth: " << depth << std::endl;
    #endif
    ```

43. ✅ **Handle Edge Cases**
    - Positions with no legal moves (checkmate/stalemate)
    - Positions with only one legal move
    - Time < 10ms (emergency mode)

44. ✅ **Add Graceful Degradation**
    ```cpp
    // If time is running out, return best move from previous depth
    // Never forfeit on time
    ```

45. ✅ **Verify UCI Protocol**
    - Test with Arena, Cutechess, Lichess
    - Ensure `bestmove` is always sent
    - Handle `stop` command immediately

---

## **EXPECTED ELO PROGRESSION**

| Phase | ELO Gain | Cumulative ELO |
|-------|----------|----------------|
| Current Code | - | ~1800 |
| Phase 1 (Fixes) | +100 | ~1900 |
| Phase 2 (Search) | +250 | ~2150 |
| Phase 3 (Eval) | +150 | ~2300 |
| Phase 4 (Time) | +50 | ~2350 |
| Phase 5 (Ordering) | +100 | ~2450 |
| Phase 6 (UCI) | +20 | ~2470 |
| Phase 7 (Endgame) | +70 | ~2540 |
| Phase 8 (Speed) | +30 | ~2570 |
| Phase 9 (Tuning) | +80 | **~2650** |

**Target: 2600-2700 ELO** (achievable without books/training)

---

## **PRIORITY ORDER FOR 1-SECOND MOVES**

**Week 1:** Phase 1 (Correctness)  
**Week 2:** Phase 2 (Search - biggest gains)  
**Week 3:** Phase 3 (Evaluation)  
**Week 4:** Phase 4 + 5 (Time + Ordering)  
**Week 5:** Phase 6 + 7 (UCI + Endgame)  
**Week 6:** Phase 8 + 9 (Optimization + Tuning)  
**Week 7:** Phase 10 (Testing & Polish)

Focus on **Phases 1-3 first** for maximum ELO gain with minimal effort. Good luck! 🚀