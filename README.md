# ♟️ Douchess (Legacy Handcrafted Edition)

**Douchess** is a bitboard-based chess engine written in C++. This repository contains the final iteration of the "Classical" development phase. It features a fully functional UCI interface, a sophisticated search tree, and a complex (though cumbersome) handcrafted evaluation function.

> [!WARNING]
> **Status: Archived / Technical Debt.** > This project has been archived. While functional, the codebase has reached a point of high complexity where manual tuning has become counter-productive. The author has opted to start a fresh project utilizing **NNUE** rather than continuing to refactor this version.

---

## 🚩 State of the Code

This project serves as a learning milestone, but it carries several structural issues that contributed to the decision to stop development:

* **Handcrafted Evaluation Fatigue:** The evaluation function relies on hundreds of "magic numbers" for piece-square tables and tactical bonuses. Fine-tuning these by hand to gain ELO became an exercise in diminishing returns.
* **Code Redundancy:** There are several **duplicate functions** and overlapping logic segments (particularly in move generation and attack detection) that make refactoring risky and exhausting.
* **Fragile Architecture:** Due to the "hand-rolled" nature of many components, fixing a bug in one area often introduced regressions in others.
* **Developer Burnout:** The author is tired of fixing the same architectural flaws and has decided that the transition to **NNUE** (Efficiently Updatable Neural Networks) is a more productive use of time.

---

## 🛠️ Technical Features (As Is)

Despite its flaws, the engine contains solid implementations of:

* **Bitboard Engine:** Efficient 64-bit representation for all pieces and occupancies.
* **PVS Search:** Principal Variation Search with Alpha-Beta pruning.
* **Transposition Table:** A large, thread-safe hash table for position memoization.
* **Search Heuristics:** Includes Aspiration Windows, Null Move Pruning, Killer Moves, and History Heuristics.
* **UCI Support:** Compatible with standard chess GUIs for testing and play.

---

## 🚀 Transition to NNUE

The lessons learned here are being applied to a new engine project. The "Handcrafted" approach is being replaced by a neural network evaluation system because:

1. **Automated Tuning:** The network learns weights through training, eliminating the need for manual centipawn adjustments.
2. **Higher Ceiling:** NNUE can understand positional nuances that are nearly impossible to code by hand.
3. **Cleaner Logic:** Moving the "intelligence" of the engine into a network file allows the core C++ code to remain lean and focused on search speed.

---

## 📝 License

This code is provided "as-is" for educational purposes under the MIT License. No further updates or bug fixes are planned for this specific repository.
