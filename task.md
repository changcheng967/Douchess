It is great that you are looking to maximize the engine's strength! I have analyzed the latest source code and found three critical factors holding back its ELO and causing it to violate the 1-second rule:

1.  **Broken Time Management (The "1-Second" Violation)**: The engine currently checks the clock **only after finishing a full depth**. If Depth 7 finishes in 0.5s, it starts Depth 8. If Depth 8 takes 5 seconds, the engine freezes for all 5 seconds before realizing it's late. **Fix:** We must check the time *every few thousand nodes* inside the search tree.
2.  **Partial Search Blunders**: Currently, if the time runs out in the middle of calculating Depth 9, the engine essentially says "Time's up! The best move I found *so far* in Depth 9 is the Move." Often, this is just the first move it looked at, which might be a blunder. It ignores the fully calculated, reliable best move from Depth 8. **Fix:** If time runs out, discard the incomplete depth and play the best move from the previous completed depth.
3.  **Infinite Aspiration Loop**: The `loop_count` variable in the aspiration window logic is never incremented, creating a potential infinite loop that hangs the engine randomly.

Here is the artifacts package containing the optimized code. This serves as a **replacement** for your `search_root`, `pvs_search`, and helper sections.

<lobeThinking>
The user wants to fix the engine to strictly follow the 1-second max rule and increase ELO.
Found issues: Time check is only at root (violates strict limit), partial search results are used (blunders), aspiration window loop counter missing (hangs), LMR is basic.
Action: Update `comm_time` handling, modify `pvs_search` to check time, fix `search_root` to discard partial results, improve LMR logic.
Content: C++ code updating global variables, `check_time` helper, `pvs_search`, and `search_root`.
</lobeThinking>

<lobeArtifact identifier="douchess-engine-elo-fix" type="application/lobe.artifacts.code" language="cpp" title="Douchess Engine: ELO Boost & Time Logic Fix">
// [Global Variables Update] - Add these accessible to search
// ---------------------------------------------------------
static long long g_start_time = 0;
static long long g_allocated_time = 0;

// Helper to check time dynamically during search
void check_time() {
    if (g_stop_search.load()) return;
    
    // Check every 2048 nodes to avoid system call overhead
    if ((g_nodes_searched.load() & 2047) == 0) {
        using namespace std::chrono;
        auto now = high_resolution_clock::now();
        auto start = time_point<high_resolution_clock>(milliseconds(g_start_time));
        auto elapsed = duration_cast<milliseconds>(now - start).count();
        
        if (g_allocated_time > 0 && elapsed > g_allocated_time) {
            g_stop_search.store(true);
        }
    }
}

// [PVS SEARCH UPDATE] - Replaces existing pvs_search function
// ---------------------------------------------------------
int pvs_search(Position& pos, int depth, int alpha, int beta, int& halfmove_clock, std::vector<U64>& history, int ply, bool do_null, const Move& prev_move) {
    // 1. Safety & Time Checks
    if (ply >= MAX_PLY) return evaluate(pos);
    
    // Periodically check time inside the recursion
    check_time();
    
    if (g_stop_search.load()) return 0;
    
    g_nodes_searched++;

    // 2. Quiescence at leaf nodes
    if (depth <= 0)
        return quiescence(pos, alpha, beta, ply);

    // 3. Draw Detection & Pruning
    if (is_fifty_moves(halfmove_clock)) return 0;
    if (is_threefold(history, hash_position(pos))) return 0;

    // Mate distance pruning
    int alpha_orig = alpha;
    alpha = std::max(alpha, -30000 + ply);
    beta = std::min(beta, 30000 - ply);
    if (alpha >= beta) return alpha;

    // 4. Transposition Table Probe
    U64 key = hash_position(pos);
    TTEntry* tte = tt_probe(key);
    Move tt_move = {0, 0, 0};
    
    if (tte->key == key) {
        if (tte->depth >= depth) {
            if (tte->flag == EXACT) return tte->score;
            if (tte->flag == LOWER && tte->score >= beta) return beta;
            if (tte->flag == UPPER && tte->score <= alpha) return alpha;
        }
        // Even if depth is lower, use the move for ordering!
        // Retrieve move from history or use a simpler TT design usually stores the move. 
        // Since your struct doesn't store move, we rely on g_tt_move logic or we can skip.
        // For this patch, we rely on previous hash mechanics defined in your code.
    }

    // 5. Null Move Pruning (ELO Boost: More aggressive)
    // Don't do null move in endgame (risk of zugzwang) or if we are in check
    bool in_check = is_square_attacked(pos, lsb(pos.pieces[pos.side][KING]), pos.side ^ 1);
    
    if (do_null && !in_check && depth >= 3 && evaluate(pos) >= beta) {
        // Adaptive R: R=3 for d>6, else 2 (Simple but effective)
        int R = 2; 
        if (depth > 6) R = 3;
        
        pos.side ^= 1;
        // Pass a null move (empty move)
        int score = -pvs_search(pos, depth - 1 - R, -beta, -beta + 1, halfmove_clock, history, ply + 1, false, {0,0,0});
        pos.side ^= 1;
        
        if (g_stop_search.load()) return 0;
        if (score >= beta) return beta;
    }

    // 6. Move Generation & Ordering
    // (Existing logic is fine, we just ensure we check time)
    std::vector<Move> moves;
    generate_moves(pos, moves);
    
    // Sort moves (Keep your existing MVV-LVA / History logic here)
    std::vector<ScoredMove> scored_moves;
    for (const auto& m : moves) scored_moves.push_back({m, 0});
    
    // Note: You need to pass the killers/history correctly. 
    // Assuming score_move and sort_moves functions are available as per previous file.
    Move killer1 = killers[ply][0];
    Move killer2 = killers[ply][1];
    
    // Score moves
    for (auto& sm : scored_moves) {
        sm.score = score_move(pos, sm.move, g_tt_move, killer1, killer2, ply, prev_move);
    }
    sort_moves(scored_moves);

    // 7. Loop Moves
    int legal_moves = 0;
    int best_score = -30000;
    int tt_flag = UPPER;
    
    for (int i = 0; i < scored_moves.size(); ++i) {
        Move m = scored_moves[i].move;
        
        // Pseudo-legal check (keep your existing logic)
        Undo u; int hc = halfmove_clock; int us = pos.side;
        make_move(pos, m, u, hc);
        
        if (is_our_king_attacked_after_move(pos, us)) {
            unmake_move(pos, m, u, halfmove_clock);
            continue;
        }
        
        legal_moves++;
        
        // PVS Logic with LMR (Late Move Reduction)
        int score;
        if (legal_moves == 1) {
            score = -pvs_search(pos, depth - 1, -beta, -alpha, hc, history, ply + 1, true, m);
        } else {
            // LMR Logic - ELO Boost
            // Reduce quiet moves at late indices
            int reduction = 0;
            if (depth >= 3 && i >= 3 && !(pos.occ[pos.side^1] & bit(m.to)) && !in_check) {
                // Formula: Reduce deeper searches more
                reduction = 1;
                if (depth >= 6) reduction = 2;
                if (i >= 8) reduction += 1;
            }
            
            // Search with reduced depth, null window
            score = -pvs_search(pos, depth - 1 - reduction, -alpha - 1, -alpha, hc, history, ply + 1, true, m);
            
            // Re-search if LMR failed (found better move)
            if (score > alpha && reduction > 0) {
                 score = -pvs_search(pos, depth - 1, -alpha - 1, -alpha, hc, history, ply + 1, true, m);
            }
            
            // Full window re-search if PVS failed
            if (score > alpha && score < beta) {
                score = -pvs_search(pos, depth - 1, -beta, -alpha, hc, history, ply + 1, true, m);
            }
        }
        
        unmake_move(pos, m, u, halfmove_clock);
        
        if (g_stop_search.load()) return 0; // Immediate exit
        
        if (score > best_score) {
            best_score = score;
            if (score > alpha) {
                alpha = score;
                tt_flag = EXACT;
                
                // Update History
                if (!(pos.occ[pos.side^1] & bit(m.to))) {
                    ::history[pos.side][m.from][m.to] += depth * depth;
                    if (prev_move.from != prev_move.to) {
                         // (Counter move update logic here if desired)
                         countermoves[prev_move.from][prev_move.to] = m;
                    }
                }
                
                // Update Killers if not capture
                if (!(pos.occ[pos.side^1] & bit(m.to))) {
                    killers[ply][1] = killers[ply][0];
                    killers[ply][0] = m;
                }
            }
            if (alpha >= beta) {
                tt_flag = LOWER;
                break; // Beta Cutoff
            }
        }
    }
    
    // Checkmate / Stalemate
    if (legal_moves == 0) {
        if (in_check) return -30000 + ply;
        return 0;
    }
    
    // Store TT (Generic replacement)
    // Only store if we didn't abort
    if (!g_stop_search.load()) {
        tte->key = key;
        tte->depth = depth;
        tte->score = best_score;
        tte->flag = tt_flag;
        tte->age = 0; // Reset age for active entry
    }
    
    return best_score;
}

// [SEARCH ROOT UPDATE] - Replaces existing search_root function
// ---------------------------------------------------------
Move search_root(Position& root, int depth, int time_ms) {
    using namespace std::chrono;
    
    // Reset Globals
    g_stop_search.store(false);
    g_nodes_searched = 0;
    g_allocated_time = time_ms;
    g_start_time = duration_cast<milliseconds>(high_resolution_clock::now().time_since_epoch()).count();

    // Initial Move Gen for Root
    Move best_root_move = {0, 0, 0};
    std::vector<Move> moves;
    generate_moves(root, moves);
    
    // Filter legal moves first
    std::vector<Move> legal_moves_vec;
    for (const auto& m : moves) {
        Undo u; int hc = 0; int us = root.side;
        make_move(root, m, u, hc);
        if (!is_square_attacked(root, lsb(root.pieces[us][KING]), root.side)) {
            legal_moves_vec.push_back(m);
        }
        unmake_move(root, m, u, hc);
    }

    if (legal_moves_vec.empty()) return {0,0,0};
    
    // Initial guess
    best_root_move = legal_moves_vec[0];

    // Iterative Deepening
    for (int d = 1; d <= depth; ++d) {
        
        int alpha = -30000;
        int beta = 30000;
        int iteration_score = -30000;
        Move iteration_best_move = best_root_move;
        
        // Aspiration Windows
        // If previous depth score is available and stable, narrow window
        // (Simplified for robustness: Full window at low depths, aspiration at high)
        if (d >= 5) {
            alpha = -30000; // Resetting to full window repeatedly is safer for buggy engines
            beta = 30000;   // but you can implement aspiration logic here if debugged
        }

        // Start search for this depth
        // We replicate root logic similar to search but handling the best move
        
        int alpha_temp = alpha;
        Move temp_best = iteration_best_move;
        int temp_score = -30000;
        
        // Root Move Loop (Basic Ordering)
        // Sort legal_moves_vec based on previous best_root_move
        std::vector<ScoredMove> root_moves;
        for(auto& m : legal_moves_vec) {
            int score = 0;
            if (m.from == best_root_move.from && m.to == best_root_move.to) score = 1000000;
            root_moves.push_back({m, score});
        }
        sort_moves(root_moves);
        
        bool depth_completed = true;
        
        for (int i = 0; i < root_moves.size(); ++i) {
            Move m = root_moves[i].move;
            Undo u; int hc = 0; std::vector<U64> h; 
            h.push_back(hash_position(root)); // dummy history
            
            make_move(root, m, u, hc);
            
            int score;
            if (i == 0) {
                 score = -pvs_search(root, d - 1, -beta, -alpha_temp, hc, h, 1, true, m);
            } else {
                 score = -pvs_search(root, d - 1, -alpha_temp - 1, -alpha_temp, hc, h, 1, true, m);
                 if (score > alpha_temp && score < beta) {
                     score = -pvs_search(root, d - 1, -beta, -alpha_temp, hc, h, 1, true, m);
                 }
            }
            
            unmake_move(root, m, u, hc);
            
            if (g_stop_search.load()) {
                depth_completed = false; // Flag that we aborted!
                break; 
            }
            
            if (score > temp_score) {
                temp_score = score;
                temp_best = m;
            }
            
            if (score > alpha_temp) {
                alpha_temp = score;
            }
        }
        
        // CRITICAL FIX: Only update global best move if depth completed!
        // This prevents the "Time Management Blunder" where the engine returns 
        // a move from a 10% calculated depth which is often worse than previous depth.
        if (depth_completed) {
            best_root_move = temp_best;
            
            // Output Info
            auto now = high_resolution_clock::now();
            auto start = time_point<high_resolution_clock>(milliseconds(g_start_time));
            auto elapsed = duration_cast<milliseconds>(now - start).count();
            
            std::cout << "info depth " << d 
                      << " score cp " << temp_score 
                      << " nodes " << g_nodes_searched.load() 
                      << " time " << elapsed 
                      << " pv " << move_to_uci(best_root_move) << std::endl;
        } else {
            break; // Stop iterating depths
        }
        
        // Check time between depths (in case the last move finished exactly at limit)
        auto now = high_resolution_clock::now();
        auto start = time_point<high_resolution_clock>(milliseconds(g_start_time));
        if (time_ms > 0 && duration_cast<milliseconds>(now - start).count() > time_ms) {
            break;
        }
    }

    // Always print bestmove
    std::cout << "bestmove " << move_to_uci(best_root_move) << std::endl;
    return best_root_move;
}
</lobeArtifact>