#include <cstdint>
#include <vector>
#include <iostream>
#include <cassert>
#include <intrin.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <random>
#include <algorithm>
#include <chrono>
#include <sstream>
#include <string>
#include <map>
#include <set>
#include <cstring> // For memset
#include <xmmintrin.h> // For prefetching

using namespace std;

using U64 = unsigned long long;

// CRITICAL FIX: Max Ply Safety Guard (Prevents Stack Overflow/Array Crashes)
constexpr int MAX_PLY = 64;

// [1] ADVANCED MOVE ORDERING & PRUNING CONSTANTS
constexpr int LMP_DEPTH = 4;
constexpr int LMP_COUNT[5] = {0, 3, 5, 8, 12}; // Pruning thresholds for depths 0-4

// [PHASE 5] Enhanced move ordering constants
constexpr int KILLER_SLOTS = 4;
constexpr int HISTORY_GRAVITY = 16384;

// Forward declarations for structs
struct Position;
struct Move;
struct Undo;

// [PHASE 6] UCI Options and constants
constexpr int TT_SIZE = 1 << 20; // 1M entries

// Enums for TT flags
enum { EXACT, LOWER, UPPER };

// TTEntry structure
struct TTEntry {
    U64 key;        // Full 64-bit key for collision detection
    int depth;
    int score;
    int flag;
    int age;        // NEW: for replacement scheme
};

// Piece values
const int pieceValue[7] = {0, 100, 320, 330, 500, 900, 20000};

// PeSTO tables (zero for now)
const int pesto_mg[7][64] = {};
const int pesto_eg[7][64] = {};
const int pesto_values[7] = {0, 100, 320, 330, 500, 900, 20000};

// Global arrays for move ordering and history
// Forward declarations to avoid premature definitions and duplicates
struct TTEntry;
extern TTEntry TT[]; // Actual definition appears later
extern Move killers[MAX_PLY][KILLER_SLOTS];
extern int history[2][64][64];
extern Move countermoves[64][64];
extern int continuation_history[2][6][64];
extern int capture_history[2][7][64];

// Forward declarations for PeSTO tables (defined later)
extern const int pesto_mg[7][64];
extern const int pesto_eg[7][64];
extern const int pesto_values[];

// Forward declarations for core functions
std::string move_to_uci(const Move& m);
bool is_valid_move(const Move& m);
void generate_moves(Position& pos, std::vector<Move>& moves);
void make_move(Position& pos, const Move& m, Undo& u, int& halfmove_clock);
void unmake_move(Position& pos, const Move& m, const Undo& u, int& halfmove_clock);
bool is_square_attacked(const Position& pos, int sq, int bySide);
int evaluate(const Position& pos);
int pvs_search(Position& pos, int depth, int alpha, int beta, int& halfmove_clock, std::vector<U64>& history, int ply, bool do_null, const Move& prev_move);
Move search_root(Position& root, int depth, int time_ms);
void uci_loop();

// Forward declarations for helper functions
int count_material(const Position& pos, int side);
int count_mobility(const Position& pos, int side, int piece_type);
int count_rooks_on_7th(const Position& pos, int side);
int count_connected_rooks(const Position& pos, int side);
int king_tropism(const Position& pos, int side);
int evaluate_pawn_structure(const Position& pos, int side);
int evaluate_king_safety(const Position& pos, int side);
int see(const Position& pos, Move m);
bool is_insufficient_material(const Position& pos);
int evaluate_king_pawn_endgame(const Position& pos, int side);
int evaluate_endgame(const Position& pos);
int evaluate_tuned(const Position& pos);
Move handle_edge_cases(Position& pos);
bool validate_position(const Position& pos);

// Forward declarations for logging
enum LogLevel { LOG_INFO, LOG_WARNING, LOG_ERROR, LOG_DEBUG };
void log(LogLevel level, const std::string& message);

// Forward declarations for testing
void run_perft_tests();
void self_play_test(int games, int depth);
void test_fixes();
void verify_uci_protocol();

// Forward declarations for initialization
void init_magics();
void init_zobrist();
void init_move_tables();
void init_search();

// Global move tables
U64 knight_moves[64];
U64 king_moves[64];

/* ===============================
   ENUMS & CONSTANTS
================================ */

enum { WHITE, BLACK };
enum { PAWN = 1, KNIGHT, BISHOP, ROOK, QUEEN, KING };

constexpr U64 notAFile = 0xfefefefefefefefeULL;
constexpr U64 notHFile = 0x7f7f7f7f7f7f7f7fULL;

/* ===============================
   BIT UTILITIES
================================ */

// Replace <bit> header functionality
inline int countr_zero(U64 x) {
    if (x == 0) return 64;
    unsigned long index;
#ifdef _MSC_VER
#if defined(_M_X64) || defined(_M_AMD64)
    // Use the intrinsic function for MSVC on 64-bit targets
    _BitScanForward64(&index, x);
#else
    // Portable alternative for 32-bit targets
    if (static_cast<uint32_t>(x) != 0) {
        _BitScanForward(&index, static_cast<uint32_t>(x));
    }
    else {
        _BitScanForward(&index, static_cast<uint32_t>(x >> 32));
        index += 32;
    }
#endif
#else
    index = __builtin_ctzll(x);
#endif
    return static_cast<int>(index);
}


inline U64 bit(int sq) {
    return 1ULL << sq;
}

// Replace _BitScanForward64/manual loop with fast intrinsic
inline int lsb(U64 b) {
    if (b == 0) return 64;
#ifdef _MSC_VER
    unsigned long idx;
    _BitScanForward64(&idx, b);
    return static_cast<int>(idx);
#else
    return __builtin_ctzll(b);
#endif
}

inline int poplsb(U64& b) {
    // CRITICAL FIX: Safe bitboard access (prevents board[64] crash)
    if (b == 0) return 64; // Return 64 for empty boards
    
    int s = lsb(b);
    b &= b - 1; // Clear the least significant bit
    return s;
}

/* ===============================
   MAGIC BITBOARDS
================================ */

struct Magic {
    U64 mask;
    U64 magic;
    int shift;
    U64* attacks;
};

Magic rookMagics[64];
Magic bishopMagics[64];

U64 rookMasks[64];
U64 bishopMasks[64];

U64 rookAttacks[64][4096];
U64 bishopAttacks[64][512];

constexpr U64 rookMagicNums[64] = {
0x8a80104000800020ULL,0x140002000100040ULL,0x2801880a0017001ULL,
0x100081001000420ULL,0x200020010080420ULL,0x3001c0002010008ULL,
0x8480008002000100ULL,0x2080088004402900ULL,
0x800098204000ULL,0x2024401000200040ULL,0x100802000801000ULL,
0x120800800801000ULL,0x208808088000400ULL,0x2802200800400ULL,
0x2200800100020080ULL,0x801000060821100ULL,
0x80044006422000ULL,0x100808020004000ULL,0x12108a0010204200ULL,
0x140848010000802ULL,0x481828014002800ULL,0x8094004002004100ULL,
0x4010040010010802ULL,0x20008806104ULL,
0x100400080208000ULL,0x2040002120081000ULL,0x21200680100081ULL,
0x20100080080080ULL,0x2000a00200410ULL,0x20080800400ULL,
0x80088400100102ULL,0x80004600042881ULL,
0x4040008040800020ULL,0x440003000200801ULL,0x4200011004500ULL,
0x188020010100100ULL,0x14800401802800ULL,0x2080040080800200ULL,
0x124080204001001ULL,0x200046502000484ULL,
0x480400080088020ULL,0x1000422010034000ULL,0x30200100110040ULL,
0x100021010009ULL,0x2002080100110004ULL,0x202008004008002ULL,
0x20020004010100ULL,0x2048440040820001ULL,
0x101002200408200ULL,0x40802000401080ULL,0x4008142004410100ULL,
0x2060820c0120200ULL,0x1001004080100ULL,0x20c020080040080ULL,
0x2935610830022400ULL,0x44440041009200ULL,0x280001040802101ULL,
0x2100190040002085ULL,0x80c0084100102001ULL,
0x4024081001000421ULL,0x20030a0244872ULL
};

constexpr U64 bishopMagicNums[64] = {
0x40040844404084ULL,0x2004208a004208ULL,0x10190041080202ULL,
0x108060845042010ULL,0x581104180800210ULL,0x2112080446200010ULL,
0x1080820820060210ULL,0x3c0808410220200ULL,
0x4050404440404ULL,0x21001420088ULL,0x24d0080801082102ULL,
0x1020a0a020400ULL,0x40308200402ULL,0x4011002100800ULL,
0x401484104104005ULL,0x801010402020200ULL,
0x400210c3880100ULL,0x404022024108200ULL,0x810018200204102ULL,
0x4002801a02003ULL,0x85040820080400ULL,0x810102c808880400ULL,
0xe900410884800ULL,0x8002020480840102ULL,
0x220200865090201ULL,0x2010100a02021202ULL,0x152048408022401ULL,
0x20080002081110ULL,0x4001001021004000ULL,0x800040400a011002ULL,
0xe4004081011002ULL,0x1c004001012080ULL,
0x8004200962a00220ULL,0x8422100208500202ULL,0x2000402200300c08ULL,
0x8646020080080080ULL,0x80020a0200100808ULL,0x2010004880111000ULL,
0x623000a080011400ULL,0x42008c0340209202ULL,
0x209188240001000ULL,0x400408a884001800ULL,0x110400a6080400ULL,
0x1840060a44020800ULL,0x90080104000041ULL,0x201011000808101ULL,
0x1a2208080504f080ULL,0x8012020600211212ULL,
0x500861011240000ULL,0x180806108200800ULL,0x4000020e01040044ULL,
0x300000261044000aULL,0x802241102020002ULL,0x20906061210001ULL,
0x5a84841004010310ULL,0x4010801011c04ULL,0xa010109502200ULL,
0x4a02012000ULL,0x500201010098b028ULL,0x8040002811040900ULL,
0x28000010020204ULL,0x6000020202d0240ULL
};

/* ===============================
   SLIDING ATTACK GENERATION
================================ */

// Magic bitboard attack functions
inline U64 rook_attacks(int sq, U64 occ) {
    U64 blockers = occ & rookMagics[sq].mask;
    int idx = static_cast<int>((blockers * rookMagics[sq].magic) >> rookMagics[sq].shift);
    return rookMagics[sq].attacks[idx];
}

inline U64 bishop_attacks(int sq, U64 occ) {
    U64 blockers = occ & bishopMagics[sq].mask;
    int idx = static_cast<int>((blockers * bishopMagics[sq].magic) >> bishopMagics[sq].shift);
    return bishopMagics[sq].attacks[idx];
}

// Keep the on-the-fly versions for initialization
U64 rook_attack_on_the_fly(int sq, U64 blockers) {
    U64 attacks = 0;
    int r = sq / 8, f = sq % 8;
    for (int rr = r + 1; rr <= 7; rr++) {
        attacks |= bit(rr * 8 + f);
        if (blockers & bit(rr * 8 + f)) break;
    }
    for (int rr = r - 1; rr >= 0; rr--) {
        attacks |= bit(rr * 8 + f);
        if (blockers & bit(rr * 8 + f)) break;
    }
    for (int ff = f + 1; ff <= 7; ff++) {
        attacks |= bit(r * 8 + ff);
        if (blockers & bit(r * 8 + ff)) break;
    }
    for (int ff = f - 1; ff >= 0; ff--) {
        attacks |= bit(r * 8 + ff);
        if (blockers & bit(r * 8 + ff)) break;
    }
    return attacks;
}

U64 bishop_attack_on_the_fly(int sq, U64 blockers) {
    U64 attacks = 0;
    int r = sq / 8, f = sq % 8;
    for (int rr = r + 1, ff = f + 1; rr <= 7 && ff <= 7; rr++, ff++) {
        attacks |= bit(rr * 8 + ff);
        if (blockers & bit(rr * 8 + ff)) break;
    }
    for (int rr = r + 1, ff = f - 1; rr <= 7 && ff >= 0; rr++, ff--) {
        attacks |= bit(rr * 8 + ff);
        if (blockers & bit(rr * 8 + ff)) break;
    }
    for (int rr = r - 1, ff = f + 1; rr >= 0 && ff <= 7; rr--, ff++) {
        attacks |= bit(rr * 8 + ff);
        if (blockers & bit(rr * 8 + ff)) break;
    }
    for (int rr = r - 1, ff = f - 1; rr >= 0 && ff >= 0; rr--, ff--) {
        attacks |= bit(rr * 8 + ff);
        if (blockers & bit(rr * 8 + ff)) break;
    }
    return attacks;
}

/* ===============================
   ZOBRIST HASHING
================================ */

/* Zobrist hashing and PST are defined before use to avoid forward-declaration issues. */

// Zobrist
U64 zobrist[2][7][64];
U64 zobristSide;
U64 zobristCastling[16];
U64 zobristEp[65]; // 0-63 squares, 64 = none

void init_zobrist() {
    std::mt19937_64 rng(123456);
    for (int s = 0; s < 2; s++)
        for (int p = 1; p <= 6; p++)
            for (int sq = 0; sq < 64; sq++)
                zobrist[s][p][sq] = rng();
    zobristSide = rng();
    
    // [FIX] Initialize castling and en passant zobrist keys
    for (int i = 0; i < 16; i++) zobristCastling[i] = rng();
    for (int i = 0; i <= 64; i++) zobristEp[i] = rng();
}

// [PHASE 1] FIX: Remove duplicate PST tables - keep only pesto_mg and pesto_eg
// The pesto tables are already defined above (lines 1441-1555)
// Delete the unused pst_midgame and pst_endgame arrays

// ===============================
// MOVE STRUCT, UNDO, AND POSITION STRUCTS
// ==============================

struct Move {
    int from, to, promo;
};

struct Undo {
    int ep, castling;
    int captured_piece; // 0 if none, else 1-6
    int captured_side;  // 0=white, 1=black
    int promo_from, promo_to; // for promotion
    int move_flags; // bit 0: en passant, bit 1: castling, bit 2: promotion
};

struct Position {
    U64 pieces[2][7]{};
    U64 occ[2]{};
    U64 all{};
    int side = WHITE;
    int ep = -1;
    int castling = 15; // KQkq

    void update() {
        occ[WHITE] = occ[BLACK] = 0;
        for (int p = 1; p <= 6; p++) {
            occ[WHITE] |= pieces[WHITE][p];
            occ[BLACK] |= pieces[BLACK][p];
        }
        all = occ[WHITE] | occ[BLACK];
    }
};

// Global UCI position and stop flag
static Position g_current_position; // will be initialized in main()
static std::atomic<bool> g_stop_search(false);
static Move g_tt_move = {0, 0, 0}; // Global TT move for move ordering

// [PHASE 8] Prefetching helper for TT
inline void prefetch_tt(U64 key) {
    // Prefetch the TT entry into cache before we need it
    // This reduces cache misses during search
    _mm_prefetch((const char*)&TT[key & (TT_SIZE - 1)], _MM_HINT_T0);
}

// [PHASE 1] Root Move Structure for Iron Dome Legality
struct RootMove {
    Move move;
    int score;
    bool operator<(const RootMove& other) const {
        return score > other.score; // Higher score first
    }
};

// Global root moves list (sanitized and legal only)
static std::vector<RootMove> g_root_moves;

// [PHASE 1] Strict Root Move Sanitization - Generates ONLY legal moves
void generate_root_moves(Position& pos, std::vector<RootMove>& root_moves) {
    root_moves.clear();
    std::vector<Move> pseudo_moves;
    generate_moves(pos, pseudo_moves);
    
    for (const auto& m : pseudo_moves) {
        Undo u;
        int hc = 0;
        int us = pos.side;
        
        make_move(pos, m, u, hc);
        
        // CRITICAL: Check if move leaves king in check
        int kingSq = lsb(pos.pieces[us][KING]);
        bool illegal = is_square_attacked(pos, kingSq, pos.side);
        
        unmake_move(pos, m, u, hc);
        
        // Only add if legal
        if (!illegal) {
            root_moves.push_back({m, 0});
        }
    }
}

// Global contempt value for draw avoidance
static int g_contempt = 10; // 10cp contempt (adjustable)

// [PHASE 4] Time management globals
static long long g_start_time = 0;
static long long g_allocated_time = 0;
static long long g_soft_time_limit = 0;
static long long g_hard_time_limit = 0;
static int g_move_stability = 0; // Track how stable the best move is
static Move g_last_best_move = {0, 0, 0};


/* ===============================
   UCI POSITION MANAGEMENT
 =============================== */

// Global variables for UCI state management
static int g_search_depth = 0;
static int g_time_ms = 0;
static int g_wtime = 0, g_btime = 0, g_winc = 0, g_binc = 0;
static std::vector<std::string> g_move_history;
static std::vector<U64> g_position_history;
static std::atomic<int> g_nodes_searched(0);

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

/* ===============================
   INITIAL POSITION
================================ */

Position start_position() {
    Position p;
    p.pieces[WHITE][PAWN] = 0x000000000000FF00ULL;
    p.pieces[WHITE][ROOK] = 0x0000000000000081ULL;
    p.pieces[WHITE][KNIGHT] = 0x0000000000000042ULL;
    p.pieces[WHITE][BISHOP] = 0x0000000000000024ULL;
    p.pieces[WHITE][QUEEN] = 0x0000000000000008ULL;
    p.pieces[WHITE][KING] = 0x0000000000000010ULL;

    p.pieces[BLACK][PAWN] = 0x00FF000000000000ULL;
    p.pieces[BLACK][ROOK] = 0x8100000000000000ULL;
    p.pieces[BLACK][KNIGHT] = 0x4200000000000000ULL;
    p.pieces[BLACK][BISHOP] = 0x2400000000000000ULL;
    p.pieces[BLACK][QUEEN] = 0x0800000000000000ULL;
    p.pieces[BLACK][KING] = 0x1000000000000000ULL;

    p.update();
    return p;
}

// ===============================
// ZOBRIST HASHING & DRAW HELPERS
// ==============================
U64 hash_position(const Position& pos) {
    U64 h = 0;
    for (int s = 0; s < 2; s++) {
        for (int p = 1; p <= 6; p++) {
            U64 bb = pos.pieces[s][p];
            while (bb) {
                int sq = lsb(bb);
                if (sq < 64) {
                    h ^= zobrist[s][p][sq];
                }
                bb &= bb - 1;
            }
        }
    }
    
    // [FIX] Include castling rights and en passant in hash
    h ^= zobristCastling[pos.castling];
    if (pos.ep != -1) h ^= zobristEp[pos.ep];
    else h ^= zobristEp[64];
    
    if (pos.side == BLACK) {
        h ^= zobristSide;
    }
    return h;
}

bool is_threefold(const std::vector<U64>& history, U64 current) {
    int count = 0;
    for (U64 h : history) if (h == current) ++count;
    return count >= 3;
}

bool is_fifty_moves(int halfmove_clock) {
    return halfmove_clock >= 100;
}

Position fen_to_position(const std::string& fen) {
    Position pos;
    std::istringstream iss(fen);

    std::string board, side, castling, ep;
    int halfmove, fullmove;
    
    iss >> board >> side >> castling >> ep >> halfmove >> fullmove;
    
    // Parse board
    int sq = 56; // Start from a8
    for (char c : board) {
        if (c == '/') {
            sq -= 16; // Go to next rank
            continue;
        }
        if (isdigit(c)) {
            sq += c - '0'; // Skip empty squares
        } else {
            int piece = 0;
            int color = (isupper(c)) ? WHITE : BLACK;
            char pc = tolower(c);
            
            switch (pc) {
                case 'p': piece = PAWN; break;
                case 'n': piece = KNIGHT; break;
                case 'b': piece = BISHOP; break;
                case 'r': piece = ROOK; break;
                case 'q': piece = QUEEN; break;
                case 'k': piece = KING; break;
            }
            
            pos.pieces[color][piece] |= bit(sq);
            sq++;
        }
    }
    
    // Parse side to move
    pos.side = (side == "w") ? WHITE : BLACK;
    
    // Parse castling
    pos.castling = 0;
    for (char c : castling) {
        switch (c) {
            case 'K': pos.castling |= 1; break;
            case 'Q': pos.castling |= 2; break;
            case 'k': pos.castling |= 4; break;
            case 'q': pos.castling |= 8; break;
        }
    }
    
    // Parse en passant
    pos.ep = -1;
    if (ep != "-") {
        int file = ep[0] - 'a';
        int rank = ep[1] - '1';
        pos.ep = rank * 8 + file;
    }
    
    pos.update();
    return pos;
}

/* ===============================
   ATTACK DETECTION
================================ */

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

// Helper: after calling make_move(pos, m) the side to move has been flipped.
// To test whether the side that just moved ('mover') has left its king in check,
// call this helper with the mover's side (the value saved BEFORE make_move).
inline bool is_our_king_attacked_after_move(const Position& pos, int mover_side) {
    // [PHASE 1] FIX: Check if king exists before using lsb
    U64 king_bb = pos.pieces[mover_side][KING];
    if (king_bb == 0) return false; // No king means no check (shouldn't happen in valid positions)
    
    int kingSq = lsb(king_bb);
    return is_square_attacked(pos, kingSq, pos.side); // pos.side is now the opponent
}

/* ===============================
   MAKE / UNMAKE
================================ */

inline int count_kings(const Position& pos, int side) {
    U64 k = pos.pieces[side][KING];
    int count = 0;
    while (k) {
        k &= k - 1;
        ++count;
    }
    return count;
}

void make_move(Position& pos, const Move& m, Undo& u, int& halfmove_clock) {
    u.ep = pos.ep;
    u.castling = pos.castling;
    u.captured_piece = 0;
    u.captured_side = pos.side ^ 1;
    u.promo_from = 0;
    u.promo_to = 0;
    u.move_flags = 0;

    int us = pos.side;
    int them = us ^ 1;
    int moved_piece = 0;
    for (int p = 1; p <= 6; p++) {
        if (pos.pieces[us][p] & bit(m.from)) {
            moved_piece = p;
            pos.pieces[us][p] ^= bit(m.from);
            break;
        }
    }
    // Promotion
    if (m.promo && moved_piece == PAWN) {
        pos.pieces[us][m.promo] |= bit(m.to);
        u.promo_from = PAWN;
        u.promo_to = m.promo;
        u.move_flags |= 4;
        halfmove_clock = 0;
    } else {
        pos.pieces[us][moved_piece] |= bit(m.to);
    }
    // Captures
    for (int p = 1; p <= 6; p++) {
        if (pos.pieces[them][p] & bit(m.to)) {
            pos.pieces[them][p] ^= bit(m.to);
            u.captured_piece = p;
            halfmove_clock = 0;
        }
    }
    // En passant
    if (m.promo == 0 && moved_piece == PAWN && m.to == pos.ep) {
        int ep_sq = m.to + ((us == WHITE) ? -8 : 8);
        pos.pieces[them][PAWN] ^= bit(ep_sq);
        u.captured_piece = PAWN;
        u.move_flags |= 1;
        halfmove_clock = 0;
    }
    // Castling
    if (moved_piece == KING && abs(m.to - m.from) == 2) {
        u.move_flags |= 2;
        if (m.to == 6) { // White kingside
            pos.pieces[WHITE][ROOK] ^= bit(7);
            pos.pieces[WHITE][ROOK] |= bit(5);
        } else if (m.to == 2) { // White queenside
            pos.pieces[WHITE][ROOK] ^= bit(0);
            pos.pieces[WHITE][ROOK] |= bit(3);
        } else if (m.to == 62) { // Black kingside
            pos.pieces[BLACK][ROOK] ^= bit(63);
            pos.pieces[BLACK][ROOK] |= bit(61);
        } else if (m.to == 58) { // Black queenside
            pos.pieces[BLACK][ROOK] ^= bit(56);
            pos.pieces[BLACK][ROOK] |= bit(59);
        }
    }
    // Update castling rights
    if (moved_piece == KING) pos.castling &= (us == WHITE) ? 0b1100 : 0b0011;
    if (moved_piece == ROOK) {
        if (us == WHITE && m.from == 0) pos.castling &= ~2;
        if (us == WHITE && m.from == 7) pos.castling &= ~1;
        if (us == BLACK && m.from == 56) pos.castling &= ~8;
        if (us == BLACK && m.from == 63) pos.castling &= ~4;
    }
    if (u.captured_piece == ROOK) {
        if (them == WHITE && m.to == 0) pos.castling &= ~2;
        if (them == WHITE && m.to == 7) pos.castling &= ~1;
        if (them == BLACK && m.to == 56) pos.castling &= ~8;
        if (them == BLACK && m.to == 63) pos.castling &= ~4;
    }
    // Set en passant
    pos.ep = -1;
    if (moved_piece == PAWN && abs(m.to - m.from) == 16) {
        int ep_sq = (m.from + m.to) / 2;
        pos.ep = ep_sq;
    }
    pos.update();
    pos.side ^= 1; // Switch sides!
    // Debug: assert exactly one king per side
    assert(count_kings(pos, WHITE) == 1 && "White must have exactly one king");
    assert(count_kings(pos, BLACK) == 1 && "Black must have exactly one king");
    if (moved_piece == PAWN || u.captured_piece) halfmove_clock = 0;
    else ++halfmove_clock;
}

void unmake_move(Position& pos, const Move& m, const Undo& u, int& halfmove_clock) {
    pos.side ^= 1;
    int us = pos.side;
    int them = us ^ 1;
    // Undo promotion
    if (u.move_flags & 4) {
        pos.pieces[us][u.promo_to] ^= bit(m.to);
        pos.pieces[us][PAWN] |= bit(m.from);
    } else {
        for (int p = 1; p <= 6; p++) {
            if (pos.pieces[us][p] & bit(m.to)) {
                pos.pieces[us][p] ^= bit(m.to);
                pos.pieces[us][p] |= bit(m.from);
                break;
            }
        }
    }
    // Undo capture
    if (u.captured_piece) {
        if (u.move_flags & 1) { // en passant
            int ep_sq = m.to + ((us == WHITE) ? -8 : 8);
            pos.pieces[them][PAWN] |= bit(ep_sq);
        } else {
            pos.pieces[them][u.captured_piece] |= bit(m.to);
        }
    }
    // Undo castling
    if (u.move_flags & 2) {
        if (m.to == 6) { // White kingside
            pos.pieces[WHITE][ROOK] ^= bit(5);
            pos.pieces[WHITE][ROOK] |= bit(7);
        } else if (m.to == 2) { // White queenside
            pos.pieces[WHITE][ROOK] ^= bit(3);
            pos.pieces[WHITE][ROOK] |= bit(0);
        } else if (m.to == 62) { // Black kingside
            pos.pieces[BLACK][ROOK] ^= bit(61);
            pos.pieces[BLACK][ROOK] |= bit(63);
        } else if (m.to == 58) { // Black queenside
            pos.pieces[BLACK][ROOK] ^= bit(59);
            pos.pieces[BLACK][ROOK] |= bit(56);
        }
    }
    pos.ep = u.ep;
    pos.castling = u.castling;
    pos.update();
    // Debug: assert exactly one king per side
    assert(count_kings(pos, WHITE) == 1 && "White must have exactly one king");
    assert(count_kings(pos, BLACK) == 1 && "Black must have exactly one king");
    // Restore halfmove clock for search
    if (u.captured_piece || (u.move_flags & 4) || (u.move_flags & 1)) {
        halfmove_clock = 0;
    } else {
        halfmove_clock--;
    }
}

/* ===============================
   MOVE GENERATION (LEGAL)
 ================================ */

inline void add_move(std::vector<Move>& moves, int from, int to, int promo = 0) {
    moves.push_back(Move{ from, to, promo });
}

// [PHASE 1] Generate only legal moves (for root validation)
// This is slower but guarantees 100% legal moves
void generate_legal_moves(Position& pos, std::vector<Move>& legal_moves) {
    std::vector<Move> pseudo_moves;
    generate_moves(pos, pseudo_moves);
    legal_moves.clear();
    
    for (const auto& m : pseudo_moves) {
        Undo u;
        int hc = 0;
        int us = pos.side;
        
        make_move(pos, m, u, hc);
        
        // Check if move leaves king in check
        int kingSq = lsb(pos.pieces[us][KING]);
        if (!is_square_attacked(pos, kingSq, pos.side)) {
            legal_moves.push_back(m);
        }
        
        unmake_move(pos, m, u, hc);
    }
}

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

/* ===============================
   DEBUG FUNCTIONS
================================ */

void debug_move_gen() {
    Position pos = start_position();
    std::vector<Move> moves;
        generate_moves(pos, moves);

    std::cout << "Moves from startpos (should be 20):" << std::endl;
    for (const auto& m : moves) {
        std::cout << move_to_uci(m) << " ";
    }
    std::cout << "\nTotal: " << moves.size() << std::endl;
}

/* ===============================
   PERFT
================================ */

unsigned long long perft(Position& pos, int depth, int& halfmove_clock, std::vector<U64>& history) {
    if (g_stop_search.load()) return 0;
    if (depth == 0) return 1;
    if (is_fifty_moves(halfmove_clock)) return 0;
    if (is_threefold(history, hash_position(pos))) return 0;
    vector<Move> moves;
    generate_moves(pos, moves);
    uint64_t nodes = 0;
    for (const auto& m : moves) {
        if (g_stop_search.load()) break;
        Undo u;
        int hc = halfmove_clock;
        int us = pos.side; // Save BEFORE make_move
        make_move(pos, m, u, hc);
        history.push_back(hash_position(pos));
        // Check legality: ensure mover's king is NOT left in check
        if (!is_our_king_attacked_after_move(pos, us))
             nodes += perft(pos, depth - 1, hc, history);
        history.pop_back();
        unmake_move(pos, m, u, halfmove_clock);
    }
    return nodes;
}

void perft_divide(Position& pos, int depth) {
    std::vector<Move> moves;
    generate_moves(pos, moves);
    int halfmove_clock = 0;
    std::vector<U64> history = { hash_position(pos) };
    uint64_t total = 0;
    for (const auto& m : moves) {
        if (g_stop_search.load()) break;
        Undo u;
        int hc = halfmove_clock;
        int us = pos.side; // Save BEFORE make_move
        make_move(pos, m, u, hc);
        history.push_back(hash_position(pos));
        // Check legality: ensure mover's king is NOT left in check
        uint64_t nodes = 0;
        if (!is_our_king_attacked_after_move(pos, us))
            nodes = perft(pos, depth - 1, hc, history);
        history.pop_back();
        unmake_move(pos, m, u, halfmove_clock);
        std::cout << move_to_uci(m) << ": " << nodes << std::endl;
        total += nodes;
    }
    std::cout << "Total: " << total << std::endl;
}

/* ===============================
   MAIN TEST
================================ */

struct PerftTest {
    Position pos;
    int depth;
    uint64_t expected;
    std::string name;
};

void test_perft_all() {
    // Startpos
    Position start = start_position();
    std::vector<PerftTest> tests = {
        { start, 1, 20, "startpos d1" },
        { start, 2, 400, "startpos d2" },
        { start, 3, 8902, "startpos d3" },
        // Add more as needed
    };
    for (const auto& t : tests) {
        int halfmove_clock = 0;
        std::vector<U64> history = { hash_position(t.pos) };
        uint64_t nodes = perft(const_cast<Position&>(t.pos), t.depth, halfmove_clock, history);
        std::cout << t.name << ": " << nodes << (nodes == t.expected ? " OK" : " FAIL") << std::endl;
    }
    std::cout << "\nPerft divide for startpos depth 3:" << std::endl;
    Position startpos = start_position();
    perft_divide(startpos, 3);
}

// Forward declaration for parse_uci_move
Move parse_uci_move(const Position& pos, const std::string& uci);

// [PHASE 9] Perft verification with detailed output
void perft_test_detailed(Position& pos, int depth, int& halfmove_clock, std::vector<U64>& history, std::map<std::string, uint64_t>& move_counts) {
    if (depth == 0) {
        return;
    }
    
    std::vector<Move> moves;
    generate_moves(pos, moves);
    
    for (const auto& m : moves) {
        Undo u;
        int hc = halfmove_clock;
        int us = pos.side;
        
        make_move(pos, m, u, hc);
        
        // Check legality
        if (!is_our_king_attacked_after_move(pos, us)) {
            std::string move_str = move_to_uci(m);
            
            if (depth == 1) {
                move_counts[move_str]++;
            } else {
                history.push_back(hash_position(pos));
                perft_test_detailed(pos, depth - 1, hc, history, move_counts);
                history.pop_back();
            }
        }
        
        unmake_move(pos, m, u, halfmove_clock);
    }
}

// [PHASE 9] Run perft tests and verify results
void run_perft_tests() {
    std::cout << "\n=== PERFT VERIFICATION ===" << std::endl;
    
    struct PerftTest {
        std::string name;
        std::string fen;
        int depth;
        uint64_t expected;
    };
    
    std::vector<PerftTest> tests = {
        {"Startpos d1", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 1, 20},
        {"Startpos d2", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 2, 400},
        {"Startpos d3", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 3, 8902},
        {"KiwiPete d1", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 1, 48},
        {"KiwiPete d2", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 2, 2039},
    };
    
    for (const auto& test : tests) {
        Position pos = fen_to_position(test.fen);
        int halfmove_clock = 0;
        std::vector<U64> history = {hash_position(pos)};
        
        auto start = std::chrono::high_resolution_clock::now();
        uint64_t nodes = perft(pos, test.depth, halfmove_clock, history);
        auto end = std::chrono::high_resolution_clock::now();
        
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        
        bool passed = (nodes == test.expected);
        std::cout << test.name << ": " << nodes << " / " << test.expected
                  << (passed ? " ✓" : " ✗") << " (" << duration << "ms)" << std::endl;
        
        if (!passed && test.depth <= 3) {
            // Show detailed breakdown for failed tests
            std::cout << "  Detailed breakdown:" << std::endl;
            std::map<std::string, uint64_t> move_counts;
            perft_test_detailed(pos, test.depth, halfmove_clock, history, move_counts);
            for (const auto& [move, count] : move_counts) {
                std::cout << "    " << move << ": " << count << std::endl;
            }
        }
    }
    
    std::cout << "=== PERFT VERIFICATION COMPLETE ===\n" << std::endl;
}

// [PHASE 9] Self-play testing function
void self_play_test(int games, int depth) {
    std::cout << "\n=== SELF-PLAY TESTING ===" << std::endl;
    std::cout << "Running " << games << " games at depth " << depth << std::endl;
    
    int white_wins = 0, black_wins = 0, draws = 0;
    
    for (int g = 0; g < games; g++) {
        Position pos = start_position();
        std::vector<U64> game_history;
        int halfmove_clock = 0;
        int move_count = 0;
        
        while (move_count < 200) { // Max 200 moves per game
            // Check for draw conditions
            if (is_fifty_moves(halfmove_clock) || is_threefold(game_history, hash_position(pos))) {
                draws++;
                break;
            }
            
            // Search for current player
            g_stop_search.store(false);
            g_nodes_searched = 0;
            g_allocated_time = 100; // 100ms per move
            g_start_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::high_resolution_clock::now().time_since_epoch()).count();
            
            // Generate root moves
            std::vector<RootMove> root_moves;
            generate_root_moves(pos, root_moves);
            
            if (root_moves.empty()) {
                // Checkmate
                if (pos.side == WHITE) {
                    black_wins++;
                } else {
                    white_wins++;
                }
                break;
            }
            
            // Simple move selection (first legal move for speed)
            Move best_move = root_moves[0].move;
            
            // Make the move
            Undo u;
            make_move(pos, best_move, u, halfmove_clock);
            game_history.push_back(hash_position(pos));
            
            move_count++;
        }
        
        if (move_count >= 200) {
            draws++;
        }
    }
    
    std::cout << "Results: White " << white_wins << " - Black " << black_wins << " - Draws " << draws << std::endl;
    std::cout << "=== SELF-PLAY TESTING COMPLETE ===\n" << std::endl;
}

// [PHASE 9] Parameter tuning structure
struct TuningParameters {
    int king_safety_weight = 15;
    int passed_pawn_weight = 10;
    int bishop_pair_bonus = 50;
    int mobility_weight = 2;
    int rook_7th_weight = 50;
    int connected_rooks_weight = 25;
    int king_tropism_weight = 15;
};

static TuningParameters g_tuning_params;

// [PHASE 9] Apply tuning parameters to evaluation
int evaluate_tuned(const Position& pos) {
    if (g_stop_search.load()) return 0;
    
    int score = 0;
    
    // Material
    int white_material = count_material(pos, WHITE);
    int black_material = count_material(pos, BLACK);
    score += white_material - black_material;
    
    // Mobility
    int white_mobility = 0, black_mobility = 0;
    for (int p = KNIGHT; p <= KING; p++) {
        white_mobility += count_mobility(pos, WHITE, p);
        black_mobility += count_mobility(pos, BLACK, p);
    }
    score += (white_mobility - black_mobility) * g_tuning_params.mobility_weight;
    
    // Rook on 7th rank
    int white_rook_7th = count_rooks_on_7th(pos, WHITE);
    int black_rook_7th = count_rooks_on_7th(pos, BLACK);
    score += (white_rook_7th - black_rook_7th) * g_tuning_params.rook_7th_weight;
    
    // Connected rooks
    int white_connected = count_connected_rooks(pos, WHITE);
    int black_connected = count_connected_rooks(pos, BLACK);
    score += (white_connected - black_connected) * g_tuning_params.connected_rooks_weight;
    
    // Pawn structure
    score += evaluate_pawn_structure(pos, WHITE) * g_tuning_params.passed_pawn_weight / 10;
    score -= evaluate_pawn_structure(pos, BLACK) * g_tuning_params.passed_pawn_weight / 10;
    
    // King tropism
    score += king_tropism(pos, WHITE) * g_tuning_params.king_tropism_weight / 15;
    score -= king_tropism(pos, BLACK) * g_tuning_params.king_tropism_weight / 15;
    
    // King safety
    score += evaluate_king_safety(pos, WHITE) * g_tuning_params.king_safety_weight / 15;
    score -= evaluate_king_safety(pos, BLACK) * g_tuning_params.king_safety_weight / 15;
    
    // PeSTO tables
    int mg_score = 0, eg_score = 0;
    for (int s = 0; s < 2; s++) {
        for (int p = 1; p <= 6; p++) {
            U64 bb = pos.pieces[s][p];
            while (bb) {
                int sq = poplsb(bb);
                int table_sq = (s == WHITE) ? sq : (63 - sq);
                if (s == WHITE) {
                    mg_score += pesto_mg[p][table_sq];
                    eg_score += pesto_eg[p][table_sq];
                } else {
                    mg_score -= pesto_mg[p][table_sq];
                    eg_score -= pesto_eg[p][table_sq];
                }
            }
        }
    }
    
    // Phase calculation
    int phase = 0;
    int total_phase = 24;
    int white_knights = __popcnt64(pos.pieces[WHITE][KNIGHT]);
    int black_knights = __popcnt64(pos.pieces[BLACK][KNIGHT]);
    int white_bishops = __popcnt64(pos.pieces[WHITE][BISHOP]);
    int black_bishops = __popcnt64(pos.pieces[BLACK][BISHOP]);
    int white_rooks = __popcnt64(pos.pieces[WHITE][ROOK]);
    int black_rooks = __popcnt64(pos.pieces[BLACK][ROOK]);
    int white_queens = __popcnt64(pos.pieces[WHITE][QUEEN]);
    int black_queens = __popcnt64(pos.pieces[BLACK][QUEEN]);
    
    phase += (white_knights + black_knights) * 1;
    phase += (white_bishops + black_bishops) * 1;
    phase += (white_rooks + black_rooks) * 2;
    phase += (white_queens + black_queens) * 4;
    
    if (phase > total_phase) phase = total_phase;
    
    int positional = ((mg_score * (total_phase - phase)) + (eg_score * phase)) / total_phase;
    score += positional;
    
    // Bishop pair bonus
    if ((int)__popcnt64(pos.pieces[WHITE][BISHOP]) >= 2) score += 75;
    if ((int)__popcnt64(pos.pieces[BLACK][BISHOP]) >= 2) score -= 75;
    
    // Contempt
    score += (pos.side == WHITE) ? g_contempt : -g_contempt;
    
    // Return from side to move perspective
    return (pos.side == WHITE) ? score : -score;
}

/* ===============================
   TEXEL TUNING FRAMEWORK
   ================================ */

// Structure to hold evaluation parameters for tuning
struct EvalParams {
    int pieceValue[7];
    int pst_midgame[7][64];
    int pst_endgame[7][64];
    int king_safety_weight;
    int passed_pawn_weight;
    int bishop_pair_bonus;
    int pawn_structure_weight;
    int mobility_weight;
};

// Global evaluation parameters
EvalParams g_eval_params;

// Initialize evaluation parameters
void init_eval_params() {
    // Copy current values
    for (int p = 0; p < 7; p++) {
        g_eval_params.pieceValue[p] = pieceValue[p];
        for (int sq = 0; sq < 64; sq++) {
            // Use pesto tables instead of pst_midgame/pst_endgame
            g_eval_params.pst_midgame[p][sq] = pesto_mg[p][sq];
            g_eval_params.pst_endgame[p][sq] = pesto_eg[p][sq];
        }
    }
    g_eval_params.king_safety_weight = 15; // Scale factor for king safety
    g_eval_params.passed_pawn_weight = 10; // Scale factor for passed pawns
    g_eval_params.bishop_pair_bonus = 50;
    g_eval_params.pawn_structure_weight = 1;
    g_eval_params.mobility_weight = 1;
}


/* ===============================
   TRANSPOSITION TABLE
================================ */


// Move ordering structures
struct ScoredMove {
    Move move;
    int score;
};

// Global arrays for move ordering and history
TTEntry TT[TT_SIZE];
Move killers[MAX_PLY][KILLER_SLOTS];
int history[2][64][64];
Move countermoves[64][64];
int continuation_history[2][6][64];
int capture_history[2][7][64];

// [PHASE 5] Enhanced MVV-LVA scoring with full SEE and capture history
int score_move(const Position& pos, const Move& m, const Move& tt_move, const Move& killer1, const Move& killer2, int ply, const Move& prev_move) {
    // [PHASE 1] FIX: TT move first - safety check for valid move
    if (tt_move.from != 0 && tt_move.to != 0 &&
        m.from == tt_move.from && m.to == tt_move.to && m.promo == tt_move.promo)
        return 10000000;
    
    // [PHASE 1] FIX: Winning captures with full SEE
    if (pos.occ[pos.side ^ 1] & bit(m.to)) {
        int victim = 0, attacker = 0;
        for (int p = 1; p <= 6; p++) {
            if (pos.pieces[pos.side ^ 1][p] & bit(m.to)) victim = p;
            if (pos.pieces[pos.side][p] & bit(m.from)) attacker = p;
        }
        
        // [PHASE 1] FIX: Use full SEE instead of approximation
        int see_score = see(pos, m);
        
        // [PHASE 5] Add capture history bonus
        int capture_bonus = 0;
        if (victim >= 1 && victim <= 6 && m.to >= 0 && m.to < 64) {
            capture_bonus = capture_history[pos.side][victim][m.to] / 10;
        }
        
        if (see_score >= 0) {
            return 1000000 + victim * 100 - attacker + capture_bonus; // Good captures
        } else {
            return -1000000 + see_score + capture_bonus; // Bad captures (losing exchanges)
        }
    }
    
    // [PHASE 5] Enhanced Killers - check multiple slots with bounds
    if (ply >= 0 && ply < MAX_PLY) {
        for (int slot = 0; slot < KILLER_SLOTS; slot++) {
            if (m.from == killers[ply][slot].from && m.to == killers[ply][slot].to) {
                return 900000 - (slot * 50000); // Decreasing priority for later slots
            }
        }
    }
    
    // [PHASE 5] Countermove heuristic with bounds checking
    if (prev_move.from != prev_move.to && prev_move.from >= 0 && prev_move.from < 64 &&
        prev_move.to >= 0 && prev_move.to < 64) {
        Move counter = countermoves[prev_move.from][prev_move.to];
        if (m.from == counter.from && m.to == counter.to && m.promo == counter.promo) {
            return 850000;
        }
    }
    
    // [PHASE 5] Continuation history with strict bounds checking
    if (prev_move.from != prev_move.to) {
        int curr_piece = -1;
        for (int p = 1; p <= 6; p++) {
            if (pos.pieces[pos.side][p] & bit(m.from)) {
                curr_piece = p - 1; // 0-indexed
                break;
            }
        }
        
        // Strict bounds checking
        if (curr_piece >= 0 && curr_piece < 6 && m.to >= 0 && m.to < 64 &&
            m.from >= 0 && m.from < 64) {
            int history_score = history[pos.side][m.from][m.to];
            int cont_score = continuation_history[pos.side][curr_piece][m.to];
            // Apply gravity to prevent overflow
            history_score = min(history_score, HISTORY_GRAVITY);
            cont_score = min(cont_score, HISTORY_GRAVITY);
            return history_score / 10 + cont_score / 20;
        }
    }
    
    // Promotions
    if (m.promo == QUEEN) return 700000;
    if (m.promo) return 600000; // Other promotions
    
    // [PHASE 5] History heuristic with gravity and bounds checking
    if (m.from >= 0 && m.from < 64 && m.to >= 0 && m.to < 64) {
        int history_score = history[pos.side][m.from][m.to];
        history_score = min(history_score, HISTORY_GRAVITY); // Apply gravity
        return history_score / 10;
    }
    
    return 0; // Fallback for invalid moves
}

// =============================================================
// [1] PRECISE STATIC EXCHANGE EVALUATION (SEE - Full Swap Algorithm)
// =============================================================
// Determines if a capture sequence is actually profitable or a blunder.
// Uses full swap algorithm with all attackers/defenders.

int get_piece_value(int piece) {
    if (piece == 0) return 0;
    // Values: P=100, N=320, B=330, R=500, Q=900, K=20000
    static const int values[] = {0, 100, 320, 330, 500, 900, 20000};
    return values[piece];
}

// Helper: Get all attackers of a square for a specific side
U64 get_attackers(const Position& pos, int sq, int side) {
    U64 attackers = 0;
    
    // Pawns
    if (side == WHITE) {
        attackers |= (pos.pieces[WHITE][PAWN] >> 7) & notHFile & bit(sq);
        attackers |= (pos.pieces[WHITE][PAWN] >> 9) & notAFile & bit(sq);
    } else {
        attackers |= (pos.pieces[BLACK][PAWN] << 7) & notAFile & bit(sq);
        attackers |= (pos.pieces[BLACK][PAWN] << 9) & notHFile & bit(sq);
    }
    
    // Knights
    U64 knights = pos.pieces[side][KNIGHT];
    while (knights) {
        int s = poplsb(knights);
        if (knight_moves[s] & bit(sq)) attackers |= bit(s);
    }
    
    // Bishops/Queens (diagonal)
    U64 bishops = pos.pieces[side][BISHOP] | pos.pieces[side][QUEEN];
    while (bishops) {
        int s = poplsb(bishops);
        if (bishop_attacks(s, pos.all) & bit(sq)) attackers |= bit(s);
    }
    
    // Rooks/Queens (straight)
    U64 rooks = pos.pieces[side][ROOK] | pos.pieces[side][QUEEN];
    while (rooks) {
        int s = poplsb(rooks);
        if (rook_attacks(s, pos.all) & bit(sq)) attackers |= bit(s);
    }
    
    // King
    U64 king = pos.pieces[side][KING];
    if (king) {
        int ks = lsb(king);
        if (king_moves[ks] & bit(sq)) attackers |= bit(ks);
    }
    
    return attackers;
}

// Helper: Get least valuable attacker from a bitboard
int get_least_valuable_attacker(const Position& pos, U64 attackers) {
    if (attackers == 0) return 0;
    
    // Check in order: Pawn, Knight, Bishop, Rook, Queen, King
    for (int p = PAWN; p <= KING; p++) {
        U64 piece_attacks = attackers & pos.pieces[pos.side][p];
        if (piece_attacks) {
            return p;
        }
    }
    return 0;
}

int see(const Position& pos, Move m) {
    int from = m.from, to = m.to, promo = m.promo;
    int us = pos.side;
    int them = us ^ 1;
    
    // 1. Identify initial victim
    int victim = 0;
    for(int p=1; p<=6; p++) {
        if(pos.pieces[them][p] & bit(to)) {
            victim = p;
            break;
        }
    }
    // Handle En Passant
    if (m.promo == 0 && (pos.pieces[us][PAWN] & bit(from)) && to == pos.ep) {
        victim = PAWN;
    }
    
    if (victim == 0) return 0; // No victim (shouldn't happen for captures)
    
    // Initial score (Value of piece we just captured)
    int score = get_piece_value(victim);
    if (promo) score += get_piece_value(promo) - get_piece_value(PAWN);
    
    // Get value of our attacking piece
    int attacker_val = 0;
    for(int p=1; p<=6; p++) {
        if(pos.pieces[us][p] & bit(from)) {
            attacker_val = get_piece_value(p);
            break;
        }
    }
    
    // If square isn't defended, we just won the piece for free
    U64 defenders = get_attackers(pos, to, them);
    if (defenders == 0) {
        return score;
    }
    
    // 2. Full Swap Algorithm
    // Simulate the capture sequence: we capture, they recapture, we recapture, etc.
    int swap_score = score - attacker_val;
    
    // Get next attacker (defender recaptures)
    U64 current_attackers = defenders;
    int current_side = them;
    int last_attacker_val = attacker_val;
    
    while (true) {
        // Find least valuable defender
        int defender_piece = get_least_valuable_attacker(pos, current_attackers);
        if (defender_piece == 0) break;
        
        int defender_val = get_piece_value(defender_piece);
        
        // If defender is more valuable than what it captures, it's a losing exchange
        if (defender_val > last_attacker_val) {
            swap_score -= defender_val;
            break;
        }
        
        // Add defender's value to score (we gain it when we recapture)
        swap_score += defender_val;
        
        // Remove this defender from future consideration
        U64 defender_bit = pos.pieces[current_side][defender_piece] & current_attackers;
        current_attackers &= ~defender_bit;
        
        // Switch sides
        current_side = us;
        last_attacker_val = defender_val;
        
        // Get our next attacker
        U64 our_attackers = get_attackers(pos, to, us);
        // Remove pieces already used
        our_attackers &= ~bit(from);
        
        int next_attacker = get_least_valuable_attacker(pos, our_attackers);
        if (next_attacker == 0) break;
        
        int next_val = get_piece_value(next_attacker);
        swap_score -= next_val;
        last_attacker_val = next_val;
        
        // Remove this attacker
        U64 next_bit = pos.pieces[us][next_attacker] & our_attackers;
        current_attackers = get_attackers(pos, to, them) & ~next_bit;
    }
    
    return swap_score;
}


// =============================================================
// [2] ADVANCED PRUNING HELPERS
// =============================================================

bool is_futility_pruning_allowed(int depth, int eval, int alpha, int ply) {
    // Only prune at low depths to prevent blunders
    if (depth >= 5) return false;
    // Don't prune if we are evaluating a mate sequence
    if (abs(alpha) > 15000) return false;
    
    int margin = 120 * depth;
    if (eval + margin < alpha) return true;
    return false;
}

// Sort moves by score
void sort_moves(std::vector<ScoredMove>& scored_moves) {
    std::sort(scored_moves.begin(), scored_moves.end(), [](const ScoredMove& a, const ScoredMove& b) {
        return a.score > b.score;
    });
}

/* ===============================
   QUIESCENCE SEARCH
================================ */

// =============================================================
// [3] QUIESCENCE SEARCH (With Check Extensions)
// =============================================================
// Fixes "Horizon Effect" by searching until the position is truly quiet.
// Now includes Check Extensions to stop resigning in solvable positions.

int quiescence(Position& pos, int alpha, int beta, int ply) {
    if (g_stop_search.load()) return 0;
    
    // Safety limit
    if (ply >= MAX_PLY) return evaluate(pos);

    // 1. Stand Pat (Static Evaluation)
    int stand_pat = evaluate(pos);
    
    // Check detection
    int kingSq = lsb(pos.pieces[pos.side][KING]);
    bool in_check = is_square_attacked(pos, kingSq, pos.side ^ 1);
    
    if (in_check) {
        // If in check, our static evaluation is invalid (we might be mated).
        // Force the search to continue.
        stand_pat = -30000 + ply;
    } else {
        // Standard Alpha-Beta pruning for Stand Pat
        if (stand_pat >= beta) return beta;
        if (alpha < stand_pat) alpha = stand_pat;
    }

    // 2. Generate Moves
    std::vector<Move> moves;
    generate_moves(pos, moves);
    
    // 3. Filter Moves
    std::vector<ScoredMove> q_moves;
    for (const auto& m : moves) {
        bool is_cap = (pos.occ[pos.side^1] & bit(m.to)) || (m.promo && m.to == pos.ep);
        
        if (in_check) {
            // IN CHECK: Search ALL evasions (Captures + Quiet)
            q_moves.push_back({m, 0});
        } else {
            // NOT IN CHECK: Only search Captures and Promotions
            if (!is_cap && !m.promo) continue;
            
            // Delta Pruning: If capture is useless, skip it (optimization)
            if (!m.promo && stand_pat + get_piece_value(QUEEN) + 200 < alpha) continue;
            
            // SEE Pruning: If capture loses material (QxP protected), skip it
            if (!m.promo && see(pos, m) < 0) continue;
            
            q_moves.push_back({m, 0});
        }
    }

    // 4. Score & Sort (MVV-LVA)
    for(auto& sm : q_moves) {
         if (pos.occ[pos.side^1] & bit(sm.move.to)) {
             int victim = 0; for(int p=1;p<=6;p++) if(pos.pieces[pos.side^1][p] & bit(sm.move.to)) victim=p;
             sm.score = victim * 100;
         }
         if (sm.move.promo) sm.score += 500;
    }
    sort_moves(q_moves);

    // 5. Recursive Search
    for (auto& sm : q_moves) {
        Move m = sm.move;
        Undo u; int dummy;
        int us = pos.side;
        make_move(pos, m, u, dummy);
        
        // Illegal move check (King capture)
        if (is_our_king_attacked_after_move(pos, us)) {
            unmake_move(pos, m, u, dummy);
            continue;
        }
        
        int score = -quiescence(pos, -beta, -alpha, ply + 1);
        unmake_move(pos, m, u, dummy);
        
        if (g_stop_search.load()) return 0;
        
        if (score >= beta) return beta;
        if (score > alpha) alpha = score;
    }
    
    return alpha;
}

// =============================================================
// [4] PVS SEARCH (Principal Variation Search - Max ELO)
// =============================================================
// Includes: IID, RFP, Check Extensions, LMR, Late Move Pruning

int pvs_search(Position& pos, int depth, int alpha, int beta, int& halfmove_clock, std::vector<U64>& history, int ply, bool do_null, const Move& prev_move) {
    // 1. Base Cases
    if (ply >= MAX_PLY) return evaluate(pos);
    check_time();
    if (g_stop_search.load()) return 0;
    g_nodes_searched++;

    // Switch to QSearch at depth 0
    if (depth <= 0) return quiescence(pos, alpha, beta, ply);

    // Draw Detection
    if (is_fifty_moves(halfmove_clock) || is_threefold(history, hash_position(pos))) return 0;

    // Mate Distance Pruning
    alpha = std::max(alpha, -30000 + ply);
    beta = std::min(beta, 30000 - ply);
    if (alpha >= beta) return alpha;

    // 2. Transposition Table Probe
    U64 key = hash_position(pos);
    TTEntry* tte = tt_probe(key);
    Move tt_move = {0,0,0};
    
    // (Note: To use IID purely, we need to know if we hit the TT.
    //  In this simplified engine, we assume if key matches, we have data.)
    bool tt_hit = (tte->key == key);

    if (tt_hit && tte->depth >= depth) {
        if (tte->flag == EXACT) return tte->score;
        if (tte->flag == LOWER && tte->score >= beta) return beta;
        if (tte->flag == UPPER && tte->score <= alpha) return alpha;
    }

    // 3. Pre-Search Logic
    bool in_check = is_square_attacked(pos, lsb(pos.pieces[pos.side][KING]), pos.side ^ 1);
    
    // Check Extension: Search deeper if King is in danger
    if (in_check) depth++;

    int eval = evaluate(pos);

    // Reverse Futility Pruning (RFP)
    // If the position is so good that "standing pat" is above beta, prune.
    if (!in_check && depth <= 6 && eval - (75 * depth) > beta) {
        return eval;
    }

    // [PHASE 2] Razoring
    if (!in_check && depth <= 3 && eval + 300 < alpha) {
        int razor_score = quiescence(pos, alpha, beta, ply);
        if (razor_score < alpha) return razor_score;
    }

    // [PHASE 2] Probcut
    if (depth >= 5 && abs(beta) < 20000) {
        int probcut_beta = beta + 200;
        int probcut_count = 0;
        // Search captures at reduced depth to see if we can prune
        std::vector<Move> moves;
        generate_moves(pos, moves);
        
        for (const auto& m : moves) {
            if (!(pos.occ[pos.side ^ 1] & bit(m.to))) continue; // Only captures
            
            Undo u; int hc = halfmove_clock;
            make_move(pos, m, u, hc);
            
            if (is_our_king_attacked_after_move(pos, pos.side ^ 1)) {
                unmake_move(pos, m, u, halfmove_clock);
                continue;
            }
            
            int score = -pvs_search(pos, depth - 3, -probcut_beta, -probcut_beta + 1, hc, history, ply + 1, true, m);
            unmake_move(pos, m, u, halfmove_clock);
            
            if (score >= probcut_beta) {
                if (g_stop_search.load()) return 0;
                return beta; // Prune this node
            }
            
            if (procut_count++ > 3) break; // Limit checks
        }
    }

    // [PHASE 2] Improved Null Move Pruning with verification
    if (do_null && !in_check && depth >= 3 && eval >= beta) {
        // Don't use NMP in endgame (zugzwang risk)
        int non_pawn_material = 0;
        for (int p = KNIGHT; p <= QUEEN; p++) {
            non_pawn_material += (int)__popcnt64(pos.pieces[WHITE][p] | pos.pieces[BLACK][p]) * pesto_values[p];
        }
        
        if (non_pawn_material >= 1300) { // Only in middlegame
            int R = 2 + (depth / 6);
            pos.side ^= 1; int old_ep = pos.ep; pos.ep = -1;
            int score = -pvs_search(pos, depth - 1 - R, -beta, -beta + 1, hc, history, ply + 1, false, {0,0,0});
            pos.ep = old_ep; pos.side ^= 1;
            if (g_stop_search.load()) return 0;
            if (score >= beta) return beta;
        }
    }

    // [PHASE 2] Multi-Cut Pruning
    if (depth >= 6 && !in_check) {
        int cutoff_count = 0;
        std::vector<Move> moves;
        generate_moves(pos, moves);
        
        // Score moves quickly
        std::vector<ScoredMove> quick_moves;
        Move k1 = killers[ply][0]; Move k2 = killers[ply][1];
        for (const auto& m : moves) {
            quick_moves.push_back({m, score_move(pos, m, tt_move, k1, k2, ply, prev_move)});
        }
        sort_moves(quick_moves);
        
        for (int i = 0; i < std::min(3, (int)quick_moves.size()); i++) {
            Move m = quick_moves[i].move;
            Undo u; int hc = halfmove_clock;
            make_move(pos, m, u, hc);
            
            if (is_our_king_attacked_after_move(pos, pos.side ^ 1)) {
                unmake_move(pos, m, u, halfmove_clock);
                continue;
            }
            
            int score = -pvs_search(pos, depth - 3, -beta, -beta + 1, hc, history, ply + 1, true, m);
            unmake_move(pos, m, u, halfmove_clock);
            
            if (score >= beta) {
                cutoff_count++;
                if (cutoff_count >= 3) return beta; // Multi-cut
            }
        }
    }

    // [PHASE 2] Internal Iterative Deepening (IID)
    // If we are in a PV node (depth is high) but have no TT move,
    // do a quick shallow search to find a "Best Move" to seed the sorting.
    if (depth >= 6 && !tt_hit) {
        int iid_depth = depth - 2;
        pvs_search(pos, iid_depth, alpha, beta, halfmove_clock, history, ply, do_null, prev_move);
        // We rely on 'g_tt_move' (global best move) being updated by this call
        if (g_tt_move.from != 0) tt_move = g_tt_move;
    }

    // 4. Move Generation
    std::vector<Move> moves;
    generate_moves(pos, moves);
    
    // 5. Scoring & Sorting
    std::vector<ScoredMove> scored_moves;
    // Get Killers
    Move k1 = killers[ply][0]; Move k2 = killers[ply][1];
    
    for (auto& m : moves) {
        // Use Global TT Move if we found one in IID or previous searches
        scored_moves.push_back({m, score_move(pos, m, g_tt_move, k1, k2, ply, prev_move)});
    }
    sort_moves(scored_moves);

    // 6. Move Loop
    int legal_moves = 0;
    int best_score = -30000;
    int tt_flag = UPPER;
    
    for (int i = 0; i < scored_moves.size(); ++i) {
        Move m = scored_moves[i].move;
        bool is_capture = (pos.occ[pos.side^1] & bit(m.to)) || (m.promo && m.to == pos.ep);
        
        // [PHASE 2] Late Move Pruning (LMP)
        // If we've searched many moves at low depth and haven't found a cutoff, stop.
        if (depth <= 4 && !in_check && !is_capture && legal_moves > (8 + depth*depth)) {
             continue;
        }

        Undo u; int hc = halfmove_clock; int us = pos.side;
        make_move(pos, m, u, hc);
        
        if (is_our_king_attacked_after_move(pos, us)) {
            unmake_move(pos, m, u, halfmove_clock);
            continue;
        }
        legal_moves++;
        
        // [PHASE 2] Singular Extensions
        // If TT move is much better than alternatives, extend search
        int extension = 0;
        if (depth >= 8 && tt_hit && m.from == tt_move.from && m.to == tt_move.to &&
            tte->depth >= depth - 3 && legal_moves == 1) {
            // Search other moves at reduced depth to see if TT move is singular
            int singular_beta = tte->score - 50 * depth;
            int singular_score = -pvs_search(pos, depth / 2, -singular_beta - 1, -singular_beta, hc, history, ply + 1, true, m);
            
            if (singular_score < singular_beta) {
                extension = 1; // TT move is singular, extend
            }
        }
        
        int score;
        if (legal_moves == 1) {
            // PV Search: Full Window
            score = -pvs_search(pos, depth - 1 + extension, -beta, -alpha, hc, history, ply + 1, true, m);
        } else {
            // [PHASE 2] Improved LMR Formula
            int R = 0;
            if (depth >= 3 && !is_capture && !in_check && i > 3) {
                // Logarithmic reduction
                R = (int)(0.75 + log(depth) * log(i) / 2.25);
                if (!extension) R++; // Increase reduction if not extended
                if (is_capture) R--; // Reduce less for captures
                if (R > depth - 2) R = depth - 2; // Don't reduce below depth 2
            }
            
            // Search with Reduced Depth and Zero Window
            score = -pvs_search(pos, depth - 1 + extension - R, -alpha - 1, -alpha, hc, history, ply + 1, true, m);
            
            // Re-search if logic failed (it was better than we thought)
            if (score > alpha && R > 0) {
                 score = -pvs_search(pos, depth - 1 + extension, -alpha - 1, -alpha, hc, history, ply + 1, true, m);
            }
            // Re-search if it beat Alpha search (found a new best move, need exact score)
            if (score > alpha && score < beta) {
                 score = -pvs_search(pos, depth - 1 + extension, -beta, -alpha, hc, history, ply + 1, true, m);
            }
        }
        
        unmake_move(pos, m, u, halfmove_clock);
        
        if (g_stop_search.load()) return 0;
        
        if (score > best_score) {
            best_score = score;
            if (score > alpha) {
                alpha = score;
                tt_flag = EXACT;
                g_tt_move = m; // Update Global Best Move for ordering
                
                if (!is_capture) {
                    ::history[pos.side][m.from][m.to] += depth * depth;
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
    
    // 7. Checkmate / Stalemate
    if (legal_moves == 0) return in_check ? -30000 + ply : 0;
    
    // 8. Store in TT
    if (!g_stop_search.load()) {
        tte->key = key;
        tte->depth = depth;
        tte->score = best_score;
        tte->flag = tt_flag;
        tte->age = 0;
    }
    
    return best_score;
}

// Helper functions for magic masks
U64 rook_mask(int sq) {
    U64 mask = 0;
    int r = sq / 8, f = sq % 8;
    // North (exclude rank 7)
    for (int rr = r + 1; rr <= 6; rr++) mask |= bit(rr * 8 + f);
    // South (exclude rank 0)
    for (int rr = r - 1; rr >= 1; rr--) mask |= bit(rr * 8 + f);
    // East (exclude file 7)
    for (int ff = f + 1; ff <= 6; ff++) mask |= bit(r * 8 + ff);
    // West (exclude file 0)
    for (int ff = f - 1; ff >= 1; ff--) mask |= bit(r * 8 + ff);
    return mask;
}

U64 bishop_mask(int sq) {
    U64 mask = 0;
    int r = sq / 8, f = sq % 8;
    // NE
    for (int rr = r + 1, ff = f + 1; rr <= 6 && ff <= 6; rr++, ff++) mask |= bit(rr * 8 + ff);
    // NW
    for (int rr = r + 1, ff = f - 1; rr <= 6 && ff >= 1; rr++, ff--) mask |= bit(rr * 8 + ff);
    // SE
    for (int rr = r - 1, ff = f + 1; rr >= 1 && ff <= 6; rr--, ff++) mask |= bit(rr * 8 + ff);
    // SW
    for (int rr = r - 1, ff = f - 1; rr >= 1 && ff >= 1; rr--, ff--) mask |= bit(rr * 8 + ff);
    return mask;
}

/* Convert move to UCI format */
std::string move_to_uci(const Move& m) {
    char from_file = 'a' + (m.from % 8);
    char from_rank = '1' + (m.from / 8);
    char to_file = 'a' + (m.to % 8);
    char to_rank = '1' + (m.to / 8);
    
    std::string result;
    result += from_file;
    result += from_rank;
    result += to_file;
    result += to_rank;
    
    if (m.promo) {
        char promo_char = 'q'; // Default to queen
        switch (m.promo) {
            case KNIGHT: promo_char = 'n'; break;
            case BISHOP: promo_char = 'b'; break;
            case ROOK: promo_char = 'r'; break;
            case QUEEN: promo_char = 'q'; break;
        }
        result += promo_char;
    }
    
    return result;
}

/* Validate if a move is within bounds */
bool is_valid_move(const Move& m) {
    return (m.from >= 0 && m.from < 64 &&
            m.to >= 0 && m.to < 64 &&
            (m.promo == 0 || (m.promo >= KNIGHT && m.promo <= QUEEN)));
}

/* Parse UCI move format */
Move parse_uci_move(const Position& pos, const std::string& uci) {
    // CRITICAL FIX: Handle promotions correctly (5 characters for promotions)
    if (uci.length() < 4 || uci.length() > 5) return {0, 0, 0};
    
    int from_file = uci[0] - 'a';
    int from_rank = uci[1] - '1';
    int to_file = uci[2] - 'a';
    int to_rank = uci[3] - '1';
    
    // Validate file and rank are within bounds
    if (from_file < 0 || from_file > 7 || from_rank < 0 || from_rank > 7 ||
        to_file < 0 || to_file > 7 || to_rank < 0 || to_rank > 7) {
        return {0, 0, 0};
    }
    
    int from = from_rank * 8 + from_file;
    int to = to_rank * 8 + to_file;
    
    int promo = 0;
    if (uci.length() == 5) {
        char promo_char = uci[4];
        switch (promo_char) {
            case 'n': promo = KNIGHT; break;
            case 'b': promo = BISHOP; break;
            case 'r': promo = ROOK; break;
            case 'q': promo = QUEEN; break;
            default: return {0, 0, 0}; // Invalid promotion character
        }
    }
    
    return {from, to, promo};
}

// Forward phase 1
void init_magics();
void init_zobrist();
void init_move_tables();
void init_search();

int main() {
    // Initialize random number generators for Zobrist hashing
    std::srand(static_cast<unsigned int>(std::time(nullptr)));
    
    // Initialize hash table size
    TTEntry* tt_entries = new TTEntry[TT_SIZE];
    for (int i = 0; i < TT_SIZE; i++) {
        tt_entries[i].key = 0;
        tt_entries[i].depth = 0;
        tt_entries[i].score = 0;
        tt_entries[i].flag = 0;
        tt_entries[i].age = 0;
    }
    
    // Initialize magic bitboards
    init_magics();
    
    // Initialize Zobrist hashing
    init_zobrist();
    
    // Initialize move tables
    init_move_tables();
    
    // Initialize search parameters
    init_search();
    
    // Print engine info
    std::cout << "Douchess Engine Initialized" << std::endl;
    std::cout << "Author: Doulet Media Developer Team" << std::endl;
    
    // UCI loop
    uci_loop();
    
    // Cleanup
    delete[] tt_entries;
    
    return 0;
}






