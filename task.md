Based on the behavior you described—the engine calculates (showing info in the panel) but never actually makes a move on the board—I have identified the specific bug in the `search_root` function.

### The Problem
In the previous fix, I added a safety check to return the "best safe move" immediately if the time ran out (`g_stop_search` is true). **However, this check used a `return` statement before printing the move to the GUI.**

When your time runs out, the engine silently returns the move internally but forgets to tell the GUI (Arena/Banksia/etc.) what that move is. The GUI sits there waiting forever.

### The Fix
We must modify the end of `search_root` to ensure `bestmove` is **always** printed, regardless of whether the search finished naturally or was stopped by time.

Here is the corrected code block for the end of the `search_root` function in `douchess.cpp`.

<lobeArtifact identifier="douchess-gui-move-fix" type="application/lobe.artifacts.code" language="cpp" title="Fix for GUI Hanging (search_root)">
// ... (inside search_root function, after the Iterative Deepening loop ends) ...

        // FIXED: Verify time limit logic inside the loop
        if (time_ms > 0 && elapsed_ms > time_ms) {
             g_stop_search.store(true);
             break; // Stop going to deeper depths
        }
    }
    
    // Final safety check: ensure we have a valid move
    // If best is still empty (shouldn't happen due to legal_moves_vec), force a random legal move
    if (best.from == 0 && best.to == 0 && best.promo == 0) {
        if (!legal_moves_vec.empty()) {
            best = legal_moves_vec[0];
        }
    }

    // CRITICAL FIX: We must print 'bestmove' BEFORE returning, even if we stopped early!
    // Previously, the 'if (g_stop_search)' block returned early, skipping this print.
    std::cout << "bestmove " << move_to_uci(best) << std::endl;
    std::cout.flush();

    return best;
}
</lobeArtifact>

### Summary of Changes
1.  **Removed the early `return best;` block**: I removed the `if (g_stop_search.load()) { return best; }` block that was preventing the print statement from executing when time ran out.
2.  **Unconditional Output**: The `std::cout << "bestmove " ...` line now runs 100% of the time right before the function exits.

Apply this change to the bottom of the `search_root` function, and the engine will reliably move on the GUI again.