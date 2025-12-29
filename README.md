# ♟️ Douchess (Legacy Handcrafted Edition)

**Douchess** is a high-performance C++ chess engine built using a **Bitboard** architecture. This project represents the completion of the "Classical" development era, featuring a robust search algorithm and a deeply tuned handcrafted evaluation function.

> [!CHECK]
> **Status: Archived.** > This project has been successfully cleaned, debugged for x64 environments, and archived. I am now transitioning to a new engine architecture utilizing **NNUE (Efficiently Updatable Neural Networks)** for evaluation.

---

## 🚩 Project Retrospective

While this engine is tactically strong and fully functional, it has reached a point of "Handcrafted Fatigue":

* **Evaluation Complexity:** Tuning hundreds of individual parameters (Piece-Square Tables, mobility bonuses, king safety) became a game of diminishing returns.
* **Technical Debt:** The codebase contains some functional duplication and "magic numbers" that make further manual scaling exhausting.
* **The Shift:** Instead of continuing to fight with manual centipawn adjustments, I am moving toward the modern standard: letting a neural network learn these positional nuances.

---

## 🛠️ Technical Features (Final State)

### 1. Board Representation

* **Bitboard Architecture:** 64-bit integer representation using an **a8=0** coordinate system.
* **Hardware Acceleration:** Full support for x64 intrinsics including `__popcnt64` and `_BitScanForward64` for lightning-fast bit manipulation.
* **Zobrist Hashing:** A complete 64-bit hashing system for position identification and repetition detection.

### 2. Search Heuristics

* **Principal Variation Search (PVS):** The core search framework for efficient alpha-beta pruning.
* **Pruning Techniques:** Includes **Aspiration Windows**, **Null Move Pruning**, **Razoring**, and **ProbCut**.
* **Move Ordering:** Optimized via **MVV-LVA**, **Killer Moves**, and **History Heuristics**.

### 3. Handcrafted Evaluation (HCE)

* **Tapered Evaluation:** Smoothly interpolates scores between Middlegame and Endgame phases.
* **Positional Knowledge:** Specialized logic for pawn structures (passed/isolated pawns), king safety, and piece mobility.

---

## 🚀 Building and Running

### Requirements

* **Platform:** Windows x64 (Required for 64-bit intrinsics).
* **Compiler:** C++17 compatible MSVC (Visual Studio).
* **Encoding:** The source file should be saved as **UTF-8 with BOM** to avoid encoding warnings (C4819).

### UCI Support

Douchess is fully compliant with the **Universal Chess Interface (UCI)** protocol. It can be loaded into any standard GUI such as Arena, CuteChess, or BanksiaGUI.

---

## ⏩ The Next Chapter: NNUE

The lessons learned from Douchess are being applied to a new project. The manual `evaluate_position` function will be replaced with a **Neural Network Forward Pass**, combining the fast search architecture developed here with the deep positional understanding of AI.

---

## 📝 License

This project is open-source and available under the MIT License. No further updates or bug fixes are planned.
