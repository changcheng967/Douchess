# ♟️ Douchess (Legacy Handcrafted Edition)

**Douchess** is a high-performance C++ chess engine using a **Bitboard** representation. This version marks the final milestone of the project’s "Classical" development phase, utilizing complex handcrafted evaluation and advanced search heuristics.

> [!WARNING]
> **Status: Archived.** > This project has been stopped and archived. While the engine is highly functional, the handcrafted evaluation (HCE) has become too difficult to scale, and the codebase contains technical debt that makes further modification exhausting. I am switching to a new engine architecture using **Stockfish-style NNUE**.

---

## 🚩 Project Retrospective & Technical Debt

This cleanup represents the final state of the engine. Several factors led to the decision to archive this project:

* **Handcrafted Evaluation Plateau:** The `evaluate_position_tapered` function balances hundreds of "magic numbers" for material, piece-square tables, and tactical bonuses. Manually tuning these centipawn values for incremental ELO gains became counter-productive.
* **Code Redundancy:** The codebase contains **duplicate functions** and overlapping logic, particularly within the move generation and attack detection systems (e.g., redundant ray-scanning for Bishops, Rooks, and Queens).
* **Maintenance Fatigue:** Fixing architectural flaws in the search-evaluation interface often introduced regressions in other modules, leading to developer burnout.

---

## 🚀 Engine Features

Despite the maintenance challenges, Douchess implements a high-level suite of chess programming techniques:

### 1. Board Representation

* **Bitboard Engine:** Uses 64-bit integers to represent the board state with an **a8=0 coordinate system**.
* **Zobrist Hashing:** A robust 64-bit hashing system for the Transposition Table, handling side-to-move, castling rights, and en-passant keys.

### 2. Search & Heuristics

* **Principal Variation Search (PVS):** A highly optimized version of Negamax that narrows the search window for non-PV nodes.
* **Advanced Pruning:** Implements **Aspiration Windows**, **Null Move Pruning**, **Razoring**, and **ProbCut** to skip irrelevant branches of the search tree.
* **Move Ordering:** Prioritizes moves using **MVV-LVA**, **Killer Moves**, **History Heuristics**, and **Static Exchange Evaluation (SEE)**.

### 3. Evaluation (HCE)

* **Tapered Evaluation:** Smoothly interpolates between Middlegame and Endgame scores based on the current phase of the game.
* **Positional Nuance:** Includes specialized logic for king safety, pawn structure (passed/backward pawns), mobility, and development bonuses.
* **Tactical Scanning:** Integrated detection for hanging pieces, trapped pieces, and tactical patterns.

---

## 🏗️ Architecture at a Glance

```text
├── douchess.cpp         # Monolithic source containing all engine logic
├── init_zobrist_keys()  # High-quality PRNG for position hashing
├── pvs_search()         # Core Principal Variation Search loop
├── quiescence()         # Tactical search to mitigate the Horizon Effect
└── evaluate_position()  # The handcrafted "brain" of the engine

```

---

## ⏩ Next Steps: Moving to NNUE

The lessons learned here—specifically the limits of manual centipawn tuning—are the foundation for my next project.

The goal is to replace the manual `evaluate_position_tapered` with a **Neural Network Forward Pass**. This allows the engine to learn positional relationships through millions of self-play games, removing the need for manual code updates and fixing the "duplicate function" maintenance loop.

---

## 📝 License

This project is open-source and available under the MIT License. No further updates or bug fixes are planned for this repository.
