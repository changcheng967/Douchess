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

using namespace std;

using U64 = unsigned long long;

// Forward declarations for structs
struct Position;
struct Move;
struct Undo;
std::string move_to_uci(const Move& m); // Forward declaration for move_to_uci
bool is_valid_move(const Move& m); // Forward declaration for move validation

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
    int s = lsb(b);
    if (s >= 64) return -1; // Invalid square
    b &= b - 1;
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
    int idx = (blockers * rookMagics[sq].magic) >> rookMagics[sq].shift;
    return rookMagics[sq].attacks[idx];
}

inline U64 bishop_attacks(int sq, U64 occ) {
    U64 blockers = occ & bishopMagics[sq].mask;
    int idx = (blockers * bishopMagics[sq].magic) >> bishopMagics[sq].shift;
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

void init_zobrist() {
    std::mt19937_64 rng(123456);
    for (int s = 0; s < 2; s++)
        for (int p = 1; p <= 6; p++)
            for (int sq = 0; sq < 64; sq++)
                zobrist[s][p][sq] = rng();
    zobristSide = rng();
}

// Correcting misplaced comment and ensuring proper declaration of pst array
// Tapered evaluation: separate midgame and endgame piece-square tables
static const int pst_midgame[7][64] = {
    /* EMPTY */ {0},
    /* PAWN */ {
         0,  0,  0,  0,  0,  0,  0,  0,
        50, 50, 50, 50, 50, 50, 50, 50,
        10, 10, 20, 30, 30, 20, 10, 10,
         5,  5, 10, 25, 25, 10,  5,  5,
         0,  0,  0, 20, 20,  0,  0,  0,
         5, -5,-10,  0,  0,-10, -5,  5,
         5, 10, 10,-20,-20, 10, 10,  5,
         0,  0,  0,  0,  0,  0,  0,  0
    },
    /* KNIGHT */ {
        -50,-40,-30,-30,-30,-30,-40,-50,
        -40,-20,  0,  5,  5,  0,-20,-40,
        -30,  5, 10, 15, 15, 10,  5,-30,
        -30,  0, 15, 20,  20, 15,  0,-30,
        -30,  5, 15, 20,  20, 15,  5,-30,
        -30,  0, 10, 15,  15, 10,  0,-30,
        -40,-20,  0,  0,  0,  0,-20,-40,
        -50,-40,-30,-30,-30,-30,-40,-50
    },
    /* BISHOP */ {
        -20,-10,-10,-10,-10,-10,-10,-20,
        -10,  5,  0,  0,  0,  0,  5,-10,
        -10, 10, 10, 10, 10, 10, 10,-10,
        -10,  0, 10, 10, 10, 10,  0,-10,
        -10,  5,  5, 10, 10,  5,  5,-10,
        -10,  0,  5, 10, 10,  5,  0,-10,
        -10,  0,  0,  0,  0,  0,  0,-10,
        -20,-10,-10,-10,-10,-10,-10,-20
    },
    /* ROOK */ {
         0,  0,  5, 10, 10,  5,  0,  0,
        -5,  0,  0,  0,  0,  0,  0, -5,
        -5,  0,  0,  0,  0,  0,  0, -5,
        -5,  0,  0,  0,  0,  0,  0, -5,
        -5,  0,  0,  0,  0,  0,  0, -5,
        -5,  0,  0,  0,  0,  0,  0, -5,
         5, 10, 10, 10, 10, 10, 10,  5,
         0,  0,  0,  0,  0,  0,  0,  0
    },
    /* QUEEN */ {
        -20,-10,-10, -5, -5,-10,-10,-20,
        -10,  0,  5,  0,  0,  0,  0,-10,
        -10,  5,  5,  5,  5,  5,  0,-10,
         -5,  0,  5,  5,  5,  5,  0, -5,
          0,  0,  5,  5,  5,  5,  0, -5,
        -10,  0,  5,  5,  5,  5,  0,-10,
        -10,  0,  0,  0,  0,  0,  0,-10,
        -20,-10,-10, -5, -5,-10,-10,-20
    },
    /* KING */ {
        -30,-40,-40,-50,-50,-40,-40,-30,
        -30,-40,-40,-50,-50,-40,-40,-30,
        -30,-40,-40,-50,-50,-40,-40,-30,
        -30,-40,-40,-50,-50,-40,-40,-30,
        -20,-30,-30,-40,-40,-30,-30,-20,
        -10,-20,-20,-20,-20,-20,-20,-10,
         20, 20,  0,  0,  0,  0, 20, 20,
         20, 30, 10,  0,  0, 10, 30, 20
    }
};

static const int pst_endgame[7][64] = {
    /* EMPTY */ {0},
    /* PAWN */ {
         0,  0,  0,  0,  0,  0,  0,  0,
        50, 50, 50, 50, 50, 50, 50, 50,
        30, 30, 40, 50, 50, 40, 30, 30,
        20, 20, 30, 40, 40, 30, 20, 20,
        10, 10, 20, 30, 30, 20, 10, 10,
        10, 10, 20, 30, 30, 20, 10, 10,
        20, 20, 30, 40, 40, 30, 20, 20,
        50, 50, 50, 50, 50, 50, 50, 50
    },
    /* KNIGHT */ {
        -50,-40,-30,-30,-30,-30,-40,-50,
        -40,-20,  0,  0,  0,  0,-20,-40,
        -30,  5, 10, 15, 15, 10,  5,-30,
        -30, 10, 15, 20, 20, 15, 10,-30,
        -30, 10, 15, 20, 20, 15, 10,-30,
        -30,  5, 10, 15, 15, 10,  5,-30,
        -40,-20,  0,  5,  5,  0,-20,-40,
        -50,-40,-30,-30,-30,-30,-40,-50
    },
    /* BISHOP */ {
        -20,-10,-10,-10,-10,-10,-10,-20,
        -10,  0,  0,  0,  0,  0,  0,-10,
        -10,  0, 10, 10, 10, 10,  0,-10,
        -10,  0, 10, 20, 20, 10,  0,-10,
        -10,  0, 10, 20, 20, 10,  0,-10,
        -10,  0, 10, 10, 10, 10,  0,-10,
        -10,  0,  0,  0,  0,  0,  0,-10,
        -20,-10,-10,-10,-10,-10,-10,-20
    },
    /* ROOK */ {
         0,  0,  0,  5,  5,  0,  0,  0,
         0,  0,  0,  5,  5,  0,  0,  0,
         0,  0,  0,  5,  5,  0,  0,  0,
         0,  0,  0,  5,  5,  0,  0,  0,
         0,  0,  0,  5,  5,  0,  0,  0,
         0,  0,  0,  5,  5,  0,  0,  0,
         0,  0,  0,  5,  5,  0,  0,  0,
         0,  0,  0,  0,  0,  0,  0,  0
    },
    /* QUEEN */ {
        -20,-10,-10, -5, -5,-10,-10,-20,
        -10,  0,  0,  0,  0,  0,  0,-10,
        -10,  0,  0,  0,  0,  0,  0,-10,
         -5,  0,  0,  0,  0,  0,  0, -5,
         -5,  0,  0,  0,  0,  0,  0, -5,
        -10,  0,  0,  0,  0,  0,  0,-10,
        -10,  0,  0,  0,  0,  0,  0,-10,
        -20,-10,-10, -5, -5,-10,-10,-20
    },
    /* KING */ {
        -50,-40,-30,-20,-20,-30,-40,-50,
        -30,-20,-10,  0,  0,-10,-20,-30,
        -30,-10, 20, 30, 30, 20,-10,-30,
        -30,-10, 30, 40, 40, 30,-10,-30,
        -30,-10, 30, 40, 40, 30,-10,-30,
        -30,-10, 20, 30, 30, 20,-10,-30,
        -30,-30,  0,  0,  0,  0,-30,-30,
        -50,-30,-30,-30,-30,-30,-30,-50
    }
};

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

// Global contempt value for draw avoidance
static int g_contempt = 10; // 10cp contempt (adjustable)

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

    /* Sliding */
    if (rook_attacks(sq, pos.all) &
        (pos.pieces[bySide][ROOK] | pos.pieces[bySide][QUEEN]))
        return true;

    if (bishop_attacks(sq, pos.all) &
        (pos.pieces[bySide][BISHOP] | pos.pieces[bySide][QUEEN]))
        return true;

    /* King */
    static const int kingOffsets[8] = { 1,-1,8,-8,9,-9,7,-7 };
    U64 king = pos.pieces[bySide][KING];
    int ks = lsb(king);
    int kr = ks / 8, kf = ks % 8;
    for (int o : kingOffsets) {
        int t = ks + o;
        if (t < 0 || t >= 64) continue;
        int tr = t / 8, tf = t % 8;
        if (abs(tr - kr) > 1 || abs(tf - kf) > 1) continue;
        if (t == sq) return true;
    }

    return false;
}

// Helper: after calling make_move(pos, m) the side to move has been flipped.
// To test whether the side that just moved ('mover') has left its king in check,
// call this helper with the mover's side (the value saved BEFORE make_move).
inline bool is_our_king_attacked_after_move(const Position& pos, int mover_side) {
    int kingSq = lsb(pos.pieces[mover_side][KING]);
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

void generate_moves(Position& pos, std::vector<Move>& moves) {
    moves.clear();
    int us = pos.side;
    int them = us ^ 1;
    U64 own = pos.occ[us];
    U64 opp = pos.occ[them];
    U64 all = pos.all;

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
        // Single push
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
        // Double push
        if (r == startRank) {
            int two = sq + 2 * forward;
            int one_sq = sq + forward;
            if (two >= 0 && two < 64 && !(all & bit(one_sq)) && !(all & bit(two))) {
                add_move(moves, sq, two);
            }
        }

        // Captures
        int capL = sq + forward - 1;
        int capR = sq + forward + 1;
        // Left capture
        if (f > 0 && capL >= 0 && capL < 64 && (opp & bit(capL))) {
            if (r == promoRank) {
                add_move(moves, sq, capL, QUEEN);
                add_move(moves, sq, capL, ROOK);
                add_move(moves, sq, capL, BISHOP);
                add_move(moves, sq, capL, KNIGHT);
            } else {
                add_move(moves, sq, capL);
            }
        }
        // Right capture
        if (f < 7 && capR >= 0 && capR < 64 && (opp & bit(capR))) {
            if (r == promoRank) {
                add_move(moves, sq, capR, QUEEN);
                add_move(moves, sq, capR, ROOK);
                add_move(moves, sq, capR, BISHOP);
                add_move(moves, sq, capR, KNIGHT);
            } else {
                add_move(moves, sq, capR);
            }
        }

        // En passant
        if (pos.ep != -1) {
            int ep_rank = (us == WHITE) ? 4 : 3;
            if (r == ep_rank) {
                // Left ep
                if (f > 0 && sq + forward - 1 == pos.ep) {
                    int ep_pawn_sq = pos.ep + ((us == WHITE) ? -8 : 8);
                    if (ep_pawn_sq >= 0 && ep_pawn_sq < 64 && (pos.pieces[them][PAWN] & bit(ep_pawn_sq)))
                        add_move(moves, sq, pos.ep);
                }
                // Right ep
                if (f < 7 && sq + forward + 1 == pos.ep) {
                    int ep_pawn_sq = pos.ep + ((us == WHITE) ? -8 : 8);
                    if (ep_pawn_sq >= 0 && ep_pawn_sq < 64 && (pos.pieces[them][PAWN] & bit(ep_pawn_sq)))
                        add_move(moves, sq, pos.ep);
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
        U64 moveset = knight_moves[sq] & ~own;
        U64 at = moveset;
        while (at)
            add_move(moves, sq, poplsb(at));
    }

    /* ===================
       SLIDING PIECES
    =================== */
    U64 bishops = pos.pieces[us][BISHOP];
    while (bishops) {
        int sq = poplsb(bishops);
        U64 at = bishop_attacks(sq, all) & ~own;
        while (at)
            add_move(moves, sq, poplsb(at));
    }

    U64 rooks = pos.pieces[us][ROOK];
    while (rooks) {
        int sq = poplsb(rooks);
        U64 at = rook_attacks(sq, all) & ~own;
        while (at)
            add_move(moves, sq, poplsb(at));
    }

    U64 queens = pos.pieces[us][QUEEN];
    while (queens) {
        int sq = poplsb(queens);
        U64 at = (rook_attacks(sq, all) |
            bishop_attacks(sq, all)) & ~own;
        while (at)
            add_move(moves, sq, poplsb(at));
    }

    /* ===================
        KING
    =================== */
    U64 king_bb = pos.pieces[us][KING];
    if (king_bb == 0) {
        // No king - position is corrupted!
        std::cerr << "ERROR: No king for side " << us << std::endl;
        return;
    }
    
    int ks = lsb(king_bb);
    if (ks >= 64) {
        std::cerr << "ERROR: Invalid king square " << ks << std::endl;
        return;
    }
    
    U64 kmoves = king_moves[ks] & ~own;
    U64 at = kmoves;
    while (at)
        add_move(moves, ks, poplsb(at));

    /* ===================
       CASTLING
    =================== */
    if (us == WHITE) {
        if ((pos.castling & 1) &&
            !(all & 0x60ULL) &&
            !is_square_attacked(pos, 4, BLACK) &&
            !is_square_attacked(pos, 5, BLACK) &&
            !is_square_attacked(pos, 6, BLACK))
            add_move(moves, 4, 6);

        if ((pos.castling & 2) &&
            !(all & 0x0EULL) &&
            !is_square_attacked(pos, 4, BLACK) &&
            !is_square_attacked(pos, 3, BLACK) &&
            !is_square_attacked(pos, 2, BLACK))
            add_move(moves, 4, 2);
    }
    else {
        if ((pos.castling & 4) &&
            !(all & (0x60ULL << 56)) &&
            !is_square_attacked(pos, 60, WHITE) &&
            !is_square_attacked(pos, 61, WHITE) &&
            !is_square_attacked(pos, 62, WHITE))
            add_move(moves, 60, 62);

        if ((pos.castling & 8) &&
            !(all & (0x0EULL << 56)) &&
            !is_square_attacked(pos, 60, WHITE) &&
            !is_square_attacked(pos, 59, WHITE) &&
            !is_square_attacked(pos, 58, WHITE))
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

/* ===============================
   EVALUATION
================================ */

static const int pieceValue[7] = {
    0, 100, 320, 330, 500, 900, 20000
};

// Helper function to count material for phase calculation
int count_material(const Position& pos, int side) {
    int material = 0;
    for (int p = 1; p <= 6; p++) {
        U64 bb = pos.pieces[side][p];
        while (bb) {
            int sq = poplsb(bb);
            if (sq >= 0) {
                material += pieceValue[p];
            }
        }
    }
    return material;
}

// Helper function to count mobility (legal moves) for a piece
int count_mobility(const Position& pos, int side, int piece_type) {
    int mobility = 0;
    U64 pieces = pos.pieces[side][piece_type];
    U64 own = pos.occ[side];
    U64 all = pos.all;
    
    while (pieces) {
        int sq = poplsb(pieces);
        
        switch (piece_type) {
            case KNIGHT: {
                U64 moves = knight_moves[sq] & ~own;
                while (moves) {
                    mobility++;
                    moves &= moves - 1;
                }
                break;
            }
            case BISHOP: {
                U64 moves = bishop_attacks(sq, all) & ~own;
                while (moves) {
                    mobility++;
                    moves &= moves - 1;
                }
                break;
            }
            case ROOK: {
                U64 moves = rook_attacks(sq, all) & ~own;
                while (moves) {
                    mobility++;
                    moves &= moves - 1;
                }
                break;
            }
            case QUEEN: {
                U64 moves = (rook_attacks(sq, all) | bishop_attacks(sq, all)) & ~own;
                while (moves) {
                    mobility++;
                    moves &= moves - 1;
                }
                break;
            }
            case KING: {
                U64 moves = king_moves[sq] & ~own;
                while (moves) {
                    mobility++;
                    moves &= moves - 1;
                }
                break;
            }
        }
    }
    return mobility;
}

// Helper function to evaluate pawn structure
int evaluate_pawn_structure(const Position& pos, int side) {
    int score = 0;
    U64 pawns = pos.pieces[side][PAWN];
    U64 all = pos.all;
    
    // Files for pawn structure analysis
    constexpr U64 fileA = 0x0101010101010101ULL;
    constexpr U64 fileB = 0x0202020202020202ULL;
    constexpr U64 fileC = 0x0404040404040404ULL;
    constexpr U64 fileD = 0x0808080808080808ULL;
    constexpr U64 fileE = 0x1010101010101010ULL;
    constexpr U64 fileF = 0x2020202020202020ULL;
    constexpr U64 fileG = 0x4040404040404040ULL;
    constexpr U64 fileH = 0x8080808080808080ULL;
    
    U64 files[8] = { fileA, fileB, fileC, fileD, fileE, fileF, fileG, fileH };
    
    // Check each file for pawn structure issues
    for (int f = 0; f < 8; f++) {
        U64 file_pawns = pawns & files[f];
        int pawn_count = 0;
        int lowest_rank = 8;
        int highest_rank = -1;
        
        // Count pawns and find their positions
        U64 temp = file_pawns;
        while (temp) {
            int sq = poplsb(temp);
            int rank = sq / 8;
            pawn_count++;
            if (rank < lowest_rank) lowest_rank = rank;
            if (rank > highest_rank) highest_rank = rank;
        }
        
        // Doubled pawns penalty
        if (pawn_count > 1) {
            score -= 20 * (pawn_count - 1);
        }
        
        // Isolated pawns penalty
        bool isolated = true;
        if (f > 0 && (pawns & files[f-1])) isolated = false;
        if (f < 7 && (pawns & files[f+1])) isolated = false;
        
        if (isolated && pawn_count > 0) {
            score -= 25;
        }
        
        // Passed pawns bonus (improved)
        if (pawn_count > 0) {
            bool passed = true;
            U64 forward_file = files[f];
            if (side == WHITE) {
                // Check files f-1, f, f+1 for enemy pawns ahead
                for (int check_f = std::max(0, f-1); check_f <= std::min(7, f+1); check_f++) {
                    U64 enemy_pawns_ahead = pos.pieces[side^1][PAWN] & files[check_f];
                    // Only check ranks ahead of our pawn
                    for (int r = highest_rank + 1; r < 8; r++) {
                        if (enemy_pawns_ahead & bit(r * 8 + check_f)) {
                            passed = false;
                            break;
                        }
                    }
                    if (!passed) break;
                }
            } else {
                // Black pawns
                for (int check_f = std::max(0, f-1); check_f <= std::min(7, f+1); check_f++) {
                    U64 enemy_pawns_ahead = pos.pieces[side^1][PAWN] & files[check_f];
                    // Only check ranks ahead of our pawn (lower ranks for black)
                    for (int r = 0; r < lowest_rank; r++) {
                        if (enemy_pawns_ahead & bit(r * 8 + check_f)) {
                            passed = false;
                            break;
                        }
                    }
                    if (!passed) break;
                }
            }
            
            if (passed) {
                // Bonus based on rank - more sophisticated scaling
                int rank_bonus = 0;
                if (side == WHITE) {
                    // White passed pawn bonus increases dramatically as it advances
                    if (highest_rank >= 5) rank_bonus = 50; // 6th rank
                    else if (highest_rank >= 4) rank_bonus = 30; // 5th rank
                    else if (highest_rank >= 3) rank_bonus = 20; // 4th rank
                    else if (highest_rank >= 2) rank_bonus = 10; // 3rd rank
                } else {
                    // Black passed pawn bonus
                    if (lowest_rank <= 2) rank_bonus = 50; // 7th rank (from black's perspective)
                    else if (lowest_rank <= 3) rank_bonus = 30; // 6th rank
                    else if (lowest_rank <= 4) rank_bonus = 20; // 5th rank
                    else if (lowest_rank <= 5) rank_bonus = 10; // 4th rank
                }
                score += rank_bonus;
            }
        }
    }
    
    return score;
}

// Helper function to evaluate king safety
int evaluate_king_safety(const Position& pos, int side) {
    int score = 0;
    int king_sq = lsb(pos.pieces[side][KING]);
    int king_rank = king_sq / 8;
    int king_file = king_sq % 8;
    
    // King tropism - enemy pieces near our king
    U64 king_area = 0;
    // Create a 3x3 area around the king
    for (int r = std::max(0, king_rank - 1); r <= std::min(7, king_rank + 1); r++) {
        for (int f = std::max(0, king_file - 1); f <= std::min(7, king_file + 1); f++) {
            king_area |= bit(r * 8 + f);
        }
    }
    
    // Penalize enemy pieces in king area
    int enemy_king_area_count = 0;
    for (int p = KNIGHT; p <= QUEEN; p++) {
        U64 enemy_pieces = pos.pieces[side^1][p] & king_area;
        while (enemy_pieces) {
            enemy_king_area_count++;
            enemy_pieces &= enemy_pieces - 1;
        }
    }
    score -= enemy_king_area_count * 15;
    
    // Pawn shield evaluation (simplified)
    if (side == WHITE) {
        // Check for pawns in front of king
        int shield_score = 0;
        if (king_rank < 7) { // Prevent out-of-bounds access
            for (int f = std::max(0, king_file - 1); f <= std::min(7, king_file + 1); f++) {
                if (pos.pieces[WHITE][PAWN] & bit((king_rank + 1) * 8 + f)) {
                    shield_score += 10;
                }
            }
        }
        score += shield_score;
    } else {
        // Black pawns
        int shield_score = 0;
        if (king_rank > 0) { // Prevent out-of-bounds access
            for (int f = std::max(0, king_file - 1); f <= std::min(7, king_file + 1); f++) {
                if (pos.pieces[BLACK][PAWN] & bit((king_rank - 1) * 8 + f)) {
                    shield_score += 10;
                }
            }
        }
        score += shield_score;
    }
    
    // Open files near king penalty
    if (king_file > 0) {
        U64 file = 0x0101010101010101ULL << (king_file - 1);
        if (!(pos.pieces[side][PAWN] & file)) {
            score -= 5; // Open file penalty
        }
    }
    if (king_file < 7) {
        U64 file = 0x0101010101010101ULL << (king_file + 1);
        if (!(pos.pieces[side][PAWN] & file)) {
            score -= 5; // Open file penalty
        }
    }
    
    return score;
}

int evaluate(const Position& pos) {
    if (g_stop_search.load()) return 0;
    int score = 0;
    
    // Calculate game phase (0 = endgame, 256 = midgame)
    int white_material = count_material(pos, WHITE);
    int black_material = count_material(pos, BLACK);
    
    // Phase calculation based on material
    int phase = 256;
    phase -= (white_material + black_material) / 64; // Simplified phase calculation
    
    for (int s = 0; s < 2; s++) {
        int sign = (s == WHITE) ? 1 : -1;
        
        // Material and PST evaluation
        for (int p = 1; p <= 6; p++) {
            U64 bb = pos.pieces[s][p];
            while (bb) {
                int sq = poplsb(bb);
                int psq = (s == WHITE) ? sq : (sq ^ 56);
                
                // Tapered evaluation: interpolate between midgame and endgame
                int mg_value = pieceValue[p] + pst_midgame[p][psq];
                int eg_value = pieceValue[p] + pst_endgame[p][psq];
                
                // Interpolate based on phase
                int value = (mg_value * phase + eg_value * (256 - phase)) / 256;
                
                score += sign * value;
            }
        }
        
        // Mobility evaluation
        int mobility = 0;
        mobility += count_mobility(pos, s, KNIGHT) * 5;   // Knights value mobility
        mobility += count_mobility(pos, s, BISHOP) * 4;   // Bishops value mobility
        mobility += count_mobility(pos, s, ROOK) * 2;     // Rooks value mobility
        mobility += count_mobility(pos, s, QUEEN) * 1;    // Queens already strong
        score += sign * mobility;
        
        // Pawn structure evaluation
        int pawn_structure = evaluate_pawn_structure(pos, s);
        score += sign * pawn_structure;
        
        // King safety evaluation
        int king_safety = evaluate_king_safety(pos, s);
        score += sign * king_safety;
    }
    
    // Add contempt - encourages winning, discourages draws
    score += (pos.side == WHITE) ? g_contempt : -g_contempt;
    
    // Bishop pair bonus
    if (__popcnt64(pos.pieces[WHITE][BISHOP]) >= 2) score += 50;
    if (__popcnt64(pos.pieces[BLACK][BISHOP]) >= 2) score -= 50;
    
    // Rook on open file bonus
    for (int s = 0; s < 2; s++) {
        int sign = (s == WHITE) ? 1 : -1;
        U64 rooks = pos.pieces[s][ROOK];
        while (rooks) {
            int sq = poplsb(rooks);
            int file = sq % 8;
            U64 file_mask = 0x0101010101010101ULL << file;
            
            // Open file (no pawns)
            if (!(pos.pieces[WHITE][PAWN] & file_mask) && !(pos.pieces[BLACK][PAWN] & file_mask)) {
                score += sign * 20;
            }
            // Semi-open file (no friendly pawns)
            else if (!(pos.pieces[s][PAWN] & file_mask)) {
                score += sign * 10;
            }
        }
    }
    
    // Knight outpost bonus
    for (int s = 0; s < 2; s++) {
        int sign = (s == WHITE) ? 1 : -1;
        U64 knights = pos.pieces[s][KNIGHT];
        while (knights) {
            int sq = poplsb(knights);
            int rank = sq / 8;
            int file = sq % 8;
            
            // Outpost: advanced, protected by pawn, can't be attacked by enemy pawns
            bool is_outpost = false;
            if (s == WHITE && rank >= 4 && rank <= 6) {
                // Check if protected by pawn
                if ((file > 0 && (pos.pieces[WHITE][PAWN] & bit(sq - 9))) ||
                    (file < 7 && (pos.pieces[WHITE][PAWN] & bit(sq - 7)))) {
                    // Check if enemy pawns can't attack
                    bool can_be_attacked = false;
                    for (int r = rank - 1; r >= 0; r--) {
                        if ((file > 0 && (pos.pieces[BLACK][PAWN] & bit(r * 8 + file - 1))) ||
                            (file < 7 && (pos.pieces[BLACK][PAWN] & bit(r * 8 + file + 1)))) {
                            can_be_attacked = true;
                            break;
                        }
                    }
                    if (!can_be_attacked) is_outpost = true;
                }
            } else if (s == BLACK && rank >= 1 && rank <= 3) {
                // Similar for black
                if ((file > 0 && (pos.pieces[BLACK][PAWN] & bit(sq + 9))) ||
                    (file < 7 && (pos.pieces[BLACK][PAWN] & bit(sq + 7)))) {
                    bool can_be_attacked = false;
                    for (int r = rank + 1; r < 8; r++) {
                        if ((file > 0 && (pos.pieces[WHITE][PAWN] & bit(r * 8 + file - 1))) ||
                            (file < 7 && (pos.pieces[WHITE][PAWN] & bit(r * 8 + file + 1)))) {
                            can_be_attacked = true;
                            break;
                        }
                    }
                    if (!can_be_attacked) is_outpost = true;
                }
            }
            
            if (is_outpost) score += sign * 30;
        }
    }
    
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
            g_eval_params.pst_midgame[p][sq] = pst_midgame[p][sq];
            g_eval_params.pst_endgame[p][sq] = pst_endgame[p][sq];
        }
    }
    g_eval_params.king_safety_weight = 15; // Scale factor for king safety
    g_eval_params.passed_pawn_weight = 10; // Scale factor for passed pawns
    g_eval_params.bishop_pair_bonus = 50;
    g_eval_params.pawn_structure_weight = 1;
    g_eval_params.mobility_weight = 1;
}

// Tuned evaluation function using parameters
int evaluate_tuned(const Position& pos) {
    if (g_stop_search.load()) return 0;
    int score = 0;
    
    // Calculate game phase (0 = endgame, 256 = midgame)
    int white_material = count_material(pos, WHITE);
    int black_material = count_material(pos, BLACK);
    
    // Phase calculation based on material
    int phase = 256;
    phase -= (white_material + black_material) / 64; // Simplified phase calculation
    
    for (int s = 0; s < 2; s++) {
        int sign = (s == WHITE) ? 1 : -1;
        
        // Material and PST evaluation with tuned parameters
        for (int p = 1; p <= 6; p++) {
            U64 bb = pos.pieces[s][p];
            while (bb) {
                int sq = poplsb(bb);
                int psq = (s == WHITE) ? sq : (sq ^ 56);
                
                // Tapered evaluation: interpolate between midgame and endgame
                int mg_value = g_eval_params.pieceValue[p] + g_eval_params.pst_midgame[p][psq];
                int eg_value = g_eval_params.pieceValue[p] + g_eval_params.pst_endgame[p][psq];
                
                // Interpolate based on phase
                int value = (mg_value * phase + eg_value * (256 - phase)) / 256;
                
                score += sign * value;
            }
        }
        
        // Mobility evaluation with tuning
        int mobility = 0;
        mobility += count_mobility(pos, s, KNIGHT) * 5 * g_eval_params.mobility_weight;   // Knights value mobility
        mobility += count_mobility(pos, s, BISHOP) * 4 * g_eval_params.mobility_weight;   // Bishops value mobility
        mobility += count_mobility(pos, s, ROOK) * 2 * g_eval_params.mobility_weight;     // Rooks value mobility
        mobility += count_mobility(pos, s, QUEEN) * 1 * g_eval_params.mobility_weight;    // Queens already strong
        score += sign * mobility;
        
        // Pawn structure evaluation with tuning
        int pawn_structure = evaluate_pawn_structure(pos, s) * g_eval_params.pawn_structure_weight;
        score += sign * pawn_structure;
        
        // King safety evaluation with tuning
        int king_safety = evaluate_king_safety(pos, s) * g_eval_params.king_safety_weight;
        score += sign * king_safety;
    }
    
    // Add contempt - encourages winning, discourages draws
    score += (pos.side == WHITE) ? g_contempt : -g_contempt;
    
    // Bishop pair bonus with tuning
    if (__popcnt64(pos.pieces[WHITE][BISHOP]) >= 2) score += g_eval_params.bishop_pair_bonus;
    if (__popcnt64(pos.pieces[BLACK][BISHOP]) >= 2) score -= g_eval_params.bishop_pair_bonus;
    
    // Rook on open file bonus
    for (int s = 0; s < 2; s++) {
        int sign = (s == WHITE) ? 1 : -1;
        U64 rooks = pos.pieces[s][ROOK];
        while (rooks) {
            int sq = poplsb(rooks);
            int file = sq % 8;
            U64 file_mask = 0x0101010101010101ULL << file;
            
            // Open file (no pawns)
            if (!(pos.pieces[WHITE][PAWN] & file_mask) && !(pos.pieces[BLACK][PAWN] & file_mask)) {
                score += sign * 20;
            }
            // Semi-open file (no friendly pawns)
            else if (!(pos.pieces[s][PAWN] & file_mask)) {
                score += sign * 10;
            }
        }
    }
    
    // Knight outpost bonus
    for (int s = 0; s < 2; s++) {
        int sign = (s == WHITE) ? 1 : -1;
        U64 knights = pos.pieces[s][KNIGHT];
        while (knights) {
            int sq = poplsb(knights);
            int rank = sq / 8;
            int file = sq % 8;
            
            // Outpost: advanced, protected by pawn, can't be attacked by enemy pawns
            bool is_outpost = false;
            if (s == WHITE && rank >= 4 && rank <= 6) {
                // Check if protected by pawn
                if ((file > 0 && (pos.pieces[WHITE][PAWN] & bit(sq - 9))) ||
                    (file < 7 && (pos.pieces[WHITE][PAWN] & bit(sq - 7)))) {
                    // Check if enemy pawns can't attack
                    bool can_be_attacked = false;
                    for (int r = rank - 1; r >= 0; r--) {
                        if ((file > 0 && (pos.pieces[BLACK][PAWN] & bit(r * 8 + file - 1))) ||
                            (file < 7 && (pos.pieces[BLACK][PAWN] & bit(r * 8 + file + 1)))) {
                            can_be_attacked = true;
                            break;
                        }
                    }
                    if (!can_be_attacked) is_outpost = true;
                }
            } else if (s == BLACK && rank >= 1 && rank <= 3) {
                // Similar for black
                if ((file > 0 && (pos.pieces[BLACK][PAWN] & bit(sq + 9))) ||
                    (file < 7 && (pos.pieces[BLACK][PAWN] & bit(sq + 7)))) {
                    bool can_be_attacked = false;
                    for (int r = rank + 1; r < 8; r++) {
                        if ((file > 0 && (pos.pieces[WHITE][PAWN] & bit(r * 8 + file - 1))) ||
                            (file < 7 && (pos.pieces[WHITE][PAWN] & bit(r * 8 + file + 1)))) {
                            can_be_attacked = true;
                            break;
                        }
                    }
                    if (!can_be_attacked) is_outpost = true;
                }
            }
            
            if (is_outpost) score += sign * 30;
        }
    }
    
    return (pos.side == WHITE) ? score : -score;
}

/* ===============================
   TRANSPOSITION TABLE
================================ */

enum { EXACT, LOWER, UPPER };

struct TTEntry {
    U64 key;        // Full 64-bit key for collision detection
    int depth;
    int score;
    int flag;
    int age;        // NEW: for replacement scheme
};

constexpr int TT_SIZE = 1 << 20;
TTEntry TT[TT_SIZE];

TTEntry* tt_probe(U64 key) {
    return &TT[key & (TT_SIZE - 1)];
}

// Move ordering structures
struct ScoredMove {
    Move move;
    int score;
};

Move killers[100][2]; // [ply][slot]
Move g_tt_move; // store last best move globally (simple)
int history[2][64][64]; // [side][from][to] - History heuristic
Move countermoves[64][64]; // [from][to] -> countermove
int continuation_history[2][6][64]; // [side][piece][to] - simplified for memory efficiency

// MVV-LVA scoring
int score_move(const Position& pos, const Move& m, const Move& tt_move, const Move& killer1, const Move& killer2, int ply, const Move& prev_move) {
    // TT move first - safety check for valid move
    if (tt_move.from != tt_move.to && m.from == tt_move.from && m.to == tt_move.to && m.promo == tt_move.promo)
        return 10000000;
    
    // Winning captures (MVV-LVA with SEE approximation)
    if (pos.occ[pos.side ^ 1] & bit(m.to)) {
        int victim = 0, attacker = 0;
        for (int p = 1; p <= 6; p++) {
            if (pos.pieces[pos.side ^ 1][p] & bit(m.to)) victim = p;
            if (pos.pieces[pos.side][p] & bit(m.from)) attacker = p;
        }
        
        // SEE (Static Exchange Evaluation) approximation
        int see_score = pieceValue[victim] - pieceValue[attacker];
        if (see_score >= 0) {
            return 1000000 + victim * 100 - attacker; // Good captures
        } else {
            return -1000000 + see_score; // Bad captures (losing exchanges)
        }
    }
    
    // Killers
    if (m.from == killer1.from && m.to == killer1.to) return 900000;
    if (m.from == killer2.from && m.to == killer2.to) return 800000;
    
    // Countermove heuristic - only if previous move is valid (not null move)
    if (prev_move.from != prev_move.to) {
        Move counter = countermoves[prev_move.from][prev_move.to];
        if (m.from == counter.from && m.to == counter.to && m.promo == counter.promo) {
            return 850000; // Between killers and history
        }
    }
    
    // Continuation history - only if previous move is valid (not null move)
    if (prev_move.from != prev_move.to) {
        int curr_piece = -1; // Initialize to invalid value
        for (int p = 1; p <= 6; p++) {
            if (pos.pieces[pos.side][p] & bit(m.from)) {
                curr_piece = p - 1; // 0-indexed
                break;
            }
        }
        
        // Safety check for array bounds - ensure piece is valid and square is valid
        if (curr_piece >= 0 && curr_piece < 6 && m.to >= 0 && m.to < 64) {
            return history[pos.side][m.from][m.to] / 10 +
                    continuation_history[pos.side][curr_piece][m.to] / 20;
        }
    }
    
    // Promotions
    if (m.promo == QUEEN) return 700000;
    
    // History heuristic
    return history[pos.side][m.from][m.to] / 10;
}

// Static Exchange Evaluation - evaluate capture sequences
int see(const Position& pos, const Move& m) {
    int from = m.from;
    int to = m.to;
    
    // Get piece values
    int attacker = 0, victim = 0;
    for (int p = 1; p <= 6; p++) {
        if (pos.pieces[pos.side][p] & bit(from)) attacker = p;
        if (pos.pieces[pos.side ^ 1][p] & bit(to)) victim = p;
    }
    
    if (victim == 0) return 0; // Not a capture
    
    // Simple SEE: victim value - attacker value
    // (Full SEE would simulate the exchange sequence)
    int see_score = pieceValue[victim] - pieceValue[attacker];
    
    // Check if square is defended
    if (is_square_attacked(pos, to, pos.side ^ 1)) {
        see_score -= pieceValue[attacker]; // We lose our piece
    }
    
    return see_score;
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

int quiescence(Position& pos, int alpha, int beta) {
    if (g_stop_search.load()) return 0;
    int stand = evaluate(pos);
    if (stand >= beta) return beta;
    if (alpha < stand) alpha = stand;
    
    // Delta pruning
    if (stand + 900 < alpha) return alpha; // Queen value
    
    std::vector<Move> moves;
    generate_moves(pos, moves);
    
    for (auto& m : moves) {
        if (g_stop_search.load()) return 0;
        // Only captures
        if (!(pos.occ[pos.side ^ 1] & bit(m.to)))
            continue;
            
        Undo u;
        int dummy = 0;
        int us = pos.side;  // Save side BEFORE make_move
        make_move(pos, m, u, dummy);
        
        // FIXED: Check if OUR king (us) is attacked by THEM (pos.side, which is now the opponent)
        int kingSq = lsb(pos.pieces[us][KING]);
        if (is_square_attacked(pos, kingSq, pos.side)) {  // Changed from pos.side ^ 1
            unmake_move(pos, m, u, dummy);
            continue;
        }
        
        int score = -quiescence(pos, -beta, -alpha);
        unmake_move(pos, m, u, dummy);
        
        if (score >= beta) return beta;
        if (score > alpha) alpha = score;
    }
    return alpha;
}

// Principal Variation Search (PVS) - More efficient than alpha-beta
int pvs_search(Position& pos, int depth, int alpha, int beta, int& halfmove_clock, std::vector<U64>& history, int ply, bool do_null, const Move& prev_move = {0, 0, 0}) {
    if (g_stop_search.load()) return 0;
    g_nodes_searched++;
    
    // Quiescence at leaf nodes
    if (depth == 0)
        return quiescence(pos, alpha, beta);
    
    // Draw detection
    if (is_fifty_moves(halfmove_clock)) return 0;
    if (is_threefold(history, hash_position(pos))) return 0;
    
    // Mate distance pruning
    int mating_value = 30000 - ply;
    if (mating_value < beta) {
        beta = mating_value;
        if (alpha >= mating_value) return mating_value;
    }
    int mated_value = -30000 + ply;
    if (mated_value > alpha) {
        alpha = mated_value;
        if (beta <= mated_value) return mated_value;
    }
    
    // Futility pruning - skip moves that can't improve position (tuned)
    if (depth <= 3 && !is_square_attacked(pos, lsb(pos.pieces[pos.side][KING]), pos.side ^ 1)) {
        int eval = evaluate(pos);
        int futility_margin = 120 * depth; // Was 100, now 120
        if (eval + futility_margin < alpha) {
            // Position is so bad that even a good move won't help
            return quiescence(pos, alpha, beta);
        }
    }
    
    // Razoring - if eval is way below alpha, go to quiescence (tuned)
    if (depth <= 3 && !is_square_attacked(pos, lsb(pos.pieces[pos.side][KING]), pos.side ^ 1)) {
        int eval = evaluate(pos);
        int razor_margin = 350 + 60 * depth; // Was 300 + 50*depth, now 350 + 60*depth
        
        if (eval + razor_margin < alpha) {
            int razor_score = quiescence(pos, alpha, beta);
            if (razor_score < alpha) {
                return razor_score;
            }
        }
    }
    
    // Transposition table probe
    U64 key = hash_position(pos);
    TTEntry* tte = tt_probe(key);
    if (tte->key == key && tte->depth >= depth) {
        if (tte->flag == EXACT) return tte->score;
        if (tte->flag == LOWER && tte->score >= beta) return beta;
        if (tte->flag == UPPER && tte->score <= alpha) return alpha;
    }
    
    // Internal Iterative Deepening - get a good TT move
    if (depth >= 4 && tte->key != key && !is_square_attacked(pos, lsb(pos.pieces[pos.side][KING]), pos.side ^ 1)) {
        // Do a reduced depth search to populate TT
        pvs_search(pos, depth - 2, alpha, beta, halfmove_clock, history, ply, false, prev_move);
        // Re-probe TT
        tte = tt_probe(key);
    }
    
    // Singular Extensions - extend if one move is clearly best
    bool singular_extension_node = false;
    Move singular_move = {0, 0, 0};
    
    if (depth >= 8 && tte->key == key && tte->depth >= depth - 3 && tte->flag != UPPER) {
        int singular_beta = tte->score - 2 * depth;
        int singular_depth = (depth - 1) / 2;
        
        // Generate moves for singular search
        std::vector<Move> singular_moves;
        generate_moves(pos, singular_moves);
        
        // Search all moves except TT move with reduced depth
        int non_tt_best = -30000; // Use proper negative infinity
        for (const auto& m : singular_moves) {
            // Skip TT move
            if (m.from == g_tt_move.from && m.to == g_tt_move.to && m.promo == g_tt_move.promo)
                continue;
            
            Undo u;
            int hc = halfmove_clock;
            int us = pos.side;  // Save side BEFORE make_move
            make_move(pos, m, u, hc);
            history.push_back(hash_position(pos));
            
            // Check legality: ensure mover's king is NOT left in check
            int kingSq = lsb(pos.pieces[us][KING]);
            if (!is_our_king_attacked_after_move(pos, us)) {
                int score = -pvs_search(pos, singular_depth, -singular_beta - 1, -singular_beta,
                                       hc, history, ply + 1, false, prev_move);
                if (score > non_tt_best) non_tt_best = score;
            }
            
            history.pop_back();
            unmake_move(pos, m, u, halfmove_clock);
            
            if (non_tt_best >= singular_beta) break; // Not singular
        }
        
        // If TT move is singular (much better than alternatives)
        if (non_tt_best < singular_beta) {
            singular_extension_node = true;
            singular_move = g_tt_move;
        }
    }
    
    // Null move pruning
    if (do_null && depth >= 3 && !is_square_attacked(pos, lsb(pos.pieces[pos.side][KING]), pos.side ^ 1)) {
        int eval = evaluate(pos);
        if (eval >= beta) {
            pos.side ^= 1;
            int score = -pvs_search(pos, depth - 3, -beta, -beta + 1, halfmove_clock, history, ply + 1, false, prev_move);
            pos.side ^= 1;
            if (score >= beta) return beta;
        }
    }
    
    // Reverse Futility Pruning (Static Null Move Pruning)
    if (depth <= 7 && !is_square_attacked(pos, lsb(pos.pieces[pos.side][KING]), pos.side ^ 1)) {
        int eval = evaluate(pos);
        int rfp_margin = 120 * depth; // 120cp per depth
        if (eval - rfp_margin >= beta) {
            return eval; // Position is so good we can prune
        }
    }
    
    // Probcut - if a shallow search shows we're way above beta, prune
    if (depth >= 5 && abs(beta) < 29000 && !is_square_attacked(pos, lsb(pos.pieces[pos.side][KING]), pos.side ^ 1)) {
        int probcut_beta = beta + 100; // 100cp margin
        int probcut_depth = depth - 4;
        
        // Try captures that might prove we're above beta
        std::vector<Move> probcut_moves;
        generate_moves(pos, probcut_moves);
        
        for (const auto& m : probcut_moves) {
            // Only try good captures
            if (!(pos.occ[pos.side ^ 1] & bit(m.to))) continue;
            
            Undo u;
            int hc = halfmove_clock;
            int us = pos.side;  // Save side BEFORE make_move
            make_move(pos, m, u, hc);
            history.push_back(hash_position(pos));
            
            // Check legality: ensure mover's king is NOT left in check
            int kingSq = lsb(pos.pieces[us][KING]);
            if (!is_our_king_attacked_after_move(pos, us)) {
                int score = -pvs_search(pos, probcut_depth, -probcut_beta, -probcut_beta + 1,
                                       hc, history, ply + 1, false, prev_move);
                
                if (score >= probcut_beta) {
                    return beta; // Probcut - position is too good
                }
            } else {
                history.pop_back();
                unmake_move(pos, m, u, halfmove_clock);
            }
        }
    }
    
    // Multi-Cut Pruning - if multiple moves fail high, position is too good
    if (depth >= 6 && !is_square_attacked(pos, lsb(pos.pieces[pos.side][KING]), pos.side ^ 1)) {
        int multicut_depth = depth - 4;
        int multicut_count = 0;
        const int multicut_threshold = 3; // Need 3 cutoffs
        
        std::vector<Move> multicut_moves;
        generate_moves(pos, multicut_moves);
        
        for (const auto& m : multicut_moves) {
            // Only try good captures and killers
            bool is_capture = (pos.occ[pos.side ^ 1] & bit(m.to));
            bool is_killer = (m.from == killers[ply][0].from && m.to == killers[ply][0].to) ||
                            (m.from == killers[ply][1].from && m.to == killers[ply][1].to);
            
            if (!is_capture && !is_killer) continue;
            
            Undo u;
            int hc = halfmove_clock;
            int us = pos.side;  // Save side BEFORE make_move
            make_move(pos, m, u, hc);
            history.push_back(hash_position(pos));
            
            // Check legality: ensure mover's king is NOT left in check
            int kingSq = lsb(pos.pieces[us][KING]);
            if (!is_our_king_attacked_after_move(pos, us)) {
                int score = -pvs_search(pos, multicut_depth, -beta, -beta + 1,
                                       hc, history, ply + 1, false, prev_move);
                
                if (score >= beta) {
                    multicut_count++;
                }
            }
            
            history.pop_back();
            unmake_move(pos, m, u, halfmove_clock);
            
            if (multicut_count >= multicut_threshold) {
                return beta; // Multi-cut - position is too good
            }
        }
    }
    
    // Generate and order moves
    std::vector<Move> moves;
    generate_moves(pos, moves);
    
    std::vector<ScoredMove> scored_moves;
    Move tt_move = g_tt_move;
    Move killer1 = killers[ply][0];
    Move killer2 = killers[ply][1];
    
    for (const auto& m : moves) {
        scored_moves.push_back({m, 0});
    }
    sort_moves(scored_moves);
    
    int legal_moves = 0;
    int best = -30000; // Use proper negative infinity for chess
    int orig_alpha = alpha;
    Move best_move = {0, 0, 0};
    
    for (int move_idx = 0; move_idx < scored_moves.size(); ++move_idx) {
        if (g_stop_search.load()) break;
        const Move& m = scored_moves[move_idx].move;
        
        // Double-check move legality during search (extra safety)
        Undo u_check;
        int hc_check = halfmove_clock;
        int us_check = pos.side;
        make_move(pos, m, u_check, hc_check);
        int kingSq_check = lsb(pos.pieces[us_check][KING]);
        if (is_square_attacked(pos, kingSq_check, pos.side)) {
            unmake_move(pos, m, u_check, halfmove_clock);
            continue;
        }
        unmake_move(pos, m, u_check, halfmove_clock);
        
        Undo u;
        int hc = halfmove_clock;
        int us = pos.side;  // Save side BEFORE make_move
        make_move(pos, m, u, hc);
        history.push_back(hash_position(pos));
        
        // Check legality: ensure mover's king is NOT left in check
        if (is_our_king_attacked_after_move(pos, us)) {
            history.pop_back();
            unmake_move(pos, m, u, halfmove_clock);
            continue;
        }
        
        // Check extension - search deeper if in check
        int extension = 0;
        if (is_square_attacked(pos, lsb(pos.pieces[pos.side][KING]), pos.side ^ 1)) {
            extension = 1;
        }
        
        // Singular extension
        if (singular_extension_node &&
            m.from == singular_move.from &&
            m.to == singular_move.to &&
            m.promo == singular_move.promo) {
            extension = 1;
        }
        
        // Move-level futility pruning
        if (depth == 1 && legal_moves > 0 && extension == 0) {
            int eval = evaluate(pos);
            if (eval + 200 < alpha && !(pos.occ[pos.side ^ 1] & bit(m.to))) {
                history.pop_back();
                unmake_move(pos, m, u, halfmove_clock);
                continue; // Skip quiet moves that can't improve position
            }
        }
        
        // SEE Pruning - prune losing captures
        if (depth <= 6 && legal_moves > 0 && (pos.occ[pos.side ^ 1] & bit(m.to))) {
            int see_score = see(pos, m);
            int see_threshold = -50 * depth; // More lenient at higher depths
            
            if (see_score < see_threshold) {
                history.pop_back();
                unmake_move(pos, m, u, halfmove_clock);
                continue; // Skip losing captures
            }
        }
        
        legal_moves++;
        
        int score;
        
        // PVS: Full window for first move, null window for rest
        if (legal_moves == 1) {
            // First move - full window search
            score = -pvs_search(pos, depth - 1 + extension, -beta, -alpha, hc, history, ply + 1, true, m);
        } else {
            // Late Move Reductions
            int reduced_depth = depth - 1 + extension;
            if (depth >= 3 && move_idx >= 3 && !(pos.occ[pos.side ^ 1] & bit(m.to)) && extension == 0) {
                int lmr_reduction = 1 + (depth >= 5) + (depth >= 8);
                reduced_depth = std::max(1, depth - 1 - lmr_reduction);
            }
            
            // Null window search
            score = -pvs_search(pos, reduced_depth, -alpha - 1, -alpha, hc, history, ply + 1, true, m);
            
            // Re-search if it failed high
            if (score > alpha && score < beta) {
                score = -pvs_search(pos, depth - 1 + extension, -beta, -alpha, hc, history, ply + 1, true, m);
            }
        }
        
        history.pop_back();
        unmake_move(pos, m, u, halfmove_clock);
        
        if (score > best) {
            best = score;
            best_move = m;
            
            // Update history heuristic
            if (!(pos.occ[pos.side ^ 1] & bit(m.to))) {
                ::history[pos.side][m.from][m.to] += depth * depth;
                
                // Update continuation history - only if previous move is valid (not null move)
                if (prev_move.from != prev_move.to) {
                    int curr_piece = -1; // Initialize to invalid value
                    for (int p = 1; p <= 6; p++) {
                        if (pos.pieces[pos.side][p] & bit(m.from)) {
                            curr_piece = p - 1; // 0-indexed
                            break;
                        }
                    }
                    
                    // Safety check for array bounds - ensure piece is valid and square is valid
                    if (curr_piece >= 0 && curr_piece < 6 && m.to >= 0 && m.to < 64) {
                        continuation_history[pos.side][curr_piece][m.to] += depth * depth;
                        
                        // Prevent overflow
                        if (continuation_history[pos.side][curr_piece][m.to] > 10000) {
                            // Age all continuation history with bounds checking
                            for (int s = 0; s < 2; s++)
                                for (int p = 0; p < 6; p++)
                                    for (int sq = 0; sq < 64; sq++)
                                        continuation_history[s][p][sq] /= 2;
                        }
                    }
                }
                
                // Prevent overflow for regular history
                if (::history[pos.side][m.from][m.to] > 10000) {
                    // Age all history scores
                    for (int s = 0; s < 2; s++)
                        for (int f = 0; f < 64; f++)
                            for (int t = 0; t < 64; t++)
                                ::history[s][f][t] /= 2;
                }
            }
            g_tt_move = m;
        }
        
        if (score > alpha) alpha = score;
        
        if (alpha >= beta) {
            // Beta cutoff - update killers and countermove
            if (!(pos.occ[pos.side ^ 1] & bit(m.to))) {
                // Safety check for killers array bounds
                if (ply < 100) {
                    killers[ply][1] = killers[ply][0];
                    killers[ply][0] = m;
                }
                
                // Update countermove - only if previous move is valid (not null move)
                if (prev_move.from != prev_move.to) {
                    countermoves[prev_move.from][prev_move.to] = m;
                }
            }
            break;
        }
    }
    
    // Checkmate or stalemate
    if (legal_moves == 0) {
        if (is_square_attacked(pos, lsb(pos.pieces[pos.side][KING]), pos.side ^ 1))
            return -30000 + ply;
        return 0;
    }
    
    // Store in transposition table with age-based replacement
    if (tte->key == 0 || tte->key == key || tte->age > 8 ||
        (tte->depth <= depth && tte->age > 2)) {
        tte->key = key;
        tte->depth = depth;
        tte->score = best;
        tte->flag = (best <= orig_alpha) ? UPPER : (best >= beta) ? LOWER : EXACT;
        tte->age = 0; // Current search
    }
    
    return best;
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

// ===============================
// MAGICS INITIALIZATION
// ==============================

void init_magics() {
    // Initialize knight moves
    for (int sq = 0; sq < 64; sq++) {
        int r = sq / 8, f = sq % 8;
        U64 moves = 0;
        int offsets[8] = { 17, 15, 10, 6, -6, -10, -15, -17 };
        for (int o : offsets) {
            int t = sq + o;
            if (t >= 0 && t < 64) {
                int r1 = sq / 8, f1 = sq % 8;
                int r2 = t / 8, f2 = t % 8;
                if (abs(r1 - r2) <= 2 && abs(f1 - f2) <= 2) {
                    moves |= bit(t);
                }
            }
        }
        knight_moves[sq] = moves;
    }
    
    // Initialize king moves
    for (int sq = 0; sq < 64; sq++) {
        int r = sq / 8, f = sq % 8;
        U64 moves = 0;
        int offsets[8] = { 1, -1, 8, -8, 9, -9, 7, -7 };
        for (int o : offsets) {
            int t = sq + o;
            if (t >= 0 && t < 64) {
                int r1 = sq / 8, f1 = sq % 8;
                int r2 = t / 8, f2 = t % 8;
                if (abs(r1 - r2) <= 1 && abs(f1 - f2) <= 1) {
                    moves |= bit(t);
                }
            }
        }
        king_moves[sq] = moves;
    }
    
    // Initialize rook and bishop masks
    for (int sq = 0; sq < 64; sq++) {
        rookMasks[sq] = rook_mask(sq);
        bishopMasks[sq] = bishop_mask(sq);
    }
    
    // Initialize rook attacks
    for (int sq = 0; sq < 64; sq++) {
        U64 mask = rookMasks[sq];
        int bits = 0;
        U64 temp = mask;
        while (temp) {
            bits++;
            temp &= temp - 1;
        }
        rookMagics[sq].mask = mask;
        rookMagics[sq].magic = rookMagicNums[sq];
        rookMagics[sq].shift = 64 - bits;
        rookMagics[sq].attacks = rookAttacks[sq];
        
        for (int i = 0; i < (1 << bits); i++) {
            U64 blockers = 0;
            U64 idx = i;
            for (int b = 0; b < 64; b++) {
                if (mask & bit(b)) {
                    if (idx & 1) blockers |= bit(b);
                    idx >>= 1;
                }
            }
            // Use magic hash as index when storing
            int magic_idx = (int)((blockers * rookMagics[sq].magic) >> rookMagics[sq].shift);
            rookAttacks[sq][magic_idx] = rook_attack_on_the_fly(sq, blockers);
        }
    }
    
    // Initialize bishop attacks
    for (int sq = 0; sq < 64; sq++) {
        U64 mask = bishopMasks[sq];
        int bits = 0;
        U64 temp = mask;
        while (temp) {
            bits++;
            temp &= temp - 1;
        }
        bishopMagics[sq].mask = mask;
        bishopMagics[sq].magic = bishopMagicNums[sq];
        bishopMagics[sq].shift = 64 - bits;
        bishopMagics[sq].attacks = bishopAttacks[sq];
        
        for (int i = 0; i < (1 << bits); i++) {
            U64 blockers = 0;
            U64 idx = i;
            for (int b = 0; b < 64; b++) {
                if (mask & bit(b)) {
                    if (idx & 1) blockers |= bit(b);
                    idx >>= 1;
                }
            }
            // Use magic hash as index when storing
            int magic_idx = (int)((blockers * bishopMagics[sq].magic) >> bishopMagics[sq].shift);
            bishopAttacks[sq][magic_idx] = bishop_attack_on_the_fly(sq, blockers);
        }
    }
}

// Initialize move tables
void init_move_tables() {
    // Initialize knight moves
    for (int sq = 0; sq < 64; sq++) {
        knight_moves[sq] = 0;
        for (int offset : { 17, 15, 10, 6, -6, -10, -15, -17 }) {
            int t = sq + offset;
            if (t >= 0 && t < 64) {
                int r1 = sq / 8, f1 = sq % 8;
                int r2 = t / 8, f2 = t % 8;
                if (abs(r1 - r2) <= 2 && abs(f1 - f2) <= 2)
                    knight_moves[sq] |= bit(t);
            }
        }
    }
    // Initialize king moves
    for (int sq = 0; sq < 64; sq++) {
        king_moves[sq] = 0;
        for (int offset : { 1, -1, 8, -8, 9, -9, 7, -7 }) {
            int t = sq + offset;
            if (t >= 0 && t < 64) {
                int r1 = sq / 8, f1 = sq % 8;
                int r2 = t / 8, f2 = t % 8;
                if (abs(r1 - r2) <= 1 && abs(f1 - f2) <= 1)
                    king_moves[sq] |= bit(t);
            }
        }
    }
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
    if (uci.length() < 4) return {0, 0, 0};
    
    int from_file = uci[0] - 'a';
    int from_rank = uci[1] - '1';
    int to_file = uci[2] - 'a';
    int to_rank = uci[3] - '1';
    
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
        }
    }
    
    return {from, to, promo};
}

// Forward declare pvs_search for use in search_root
int pvs_search(Position& pos, int depth, int alpha, int beta, int& halfmove_clock, std::vector<U64>& history, int ply, bool do_null, const Move& prev_move);

// Search root function (supports time control and stop flag)
Move search_root(Position& root, int depth, int time_ms) {
    using namespace std::chrono;
    auto start = high_resolution_clock::now();
    
    Move best = {0, 0, 0};
    int bestScore = -30000; // Use proper negative infinity for chess
    
    std::vector<Move> moves;
    generate_moves(root, moves);
    
    // If no moves available, return null move (shouldn't happen in normal chess)
    if (moves.empty()) {
        return best;
    }
    
    // === THE "MOVE 2" STABILITY PATCH ===
    // Pre-filter moves to ensure we always have a legal fallback
    std::vector<Move> legal_moves_vec;
    for (const auto& m : moves) {
        Undo u;
        int hc = 0;
        int us = root.side;
        make_move(root, m, u, hc);
        if (!is_square_attacked(root, lsb(root.pieces[us][KING]), root.side)) {
            legal_moves_vec.push_back(m);
        }
        unmake_move(root, m, u, hc);
    }
    
    if (legal_moves_vec.empty()) {
        // We are actually checkmated or stalemated
        return {0, 0, 0};
    }
    
    // Set a guaranteed legal move as the initial best
    best = legal_moves_vec[0];
    
    int halfmove_clock = 0;
    std::vector<U64> history = { hash_position(root) };
    g_stop_search.store(false);
    g_nodes_searched = 0;
    
    // Age TT entries at start of new search
    for (int i = 0; i < TT_SIZE; i++) {
        if (TT[i].key != 0) {
            TT[i].age++;
        }
    }
    
    // Move ordering from previous iteration - use legal moves only
    std::vector<ScoredMove> scored_moves;
    for (const auto& m : legal_moves_vec) {
        scored_moves.push_back({m, 0});
    }
    
    int legal_moves_count = 0;
    
    for (int d = 1; d <= depth && !g_stop_search.load(); ++d) {
        int alpha = -100000;
        int beta = 100000;
        
        // Aspiration windows for depth >= 5
        if (d >= 5) {
            int delta = 25;
            alpha = std::max(-100000, bestScore - delta);
            beta = std::min(100000, bestScore + delta);
        }
        
        bool research = false;
        int iteration_best = -30000; // Use proper negative infinity
        Move iteration_best_move = best;
        
        int loop_count = 0;
        do {
            // Prevent infinite loops in aspiration windows
            if (loop_count > 10) {
                break;
            }
            
            research = false;
            iteration_best = -30000; // Use proper negative infinity
            int search_alpha = alpha;
            

            
            for (int move_idx = 0; move_idx < scored_moves.size(); ++move_idx) {
                if (g_stop_search.load()) break;
                const Move& m = scored_moves[move_idx].move;
                
                // Double-check move legality during search (extra safety)
                Undo u_check;
                int hc_check = halfmove_clock;
                int us_check = root.side;
                make_move(root, m, u_check, hc_check);
                int kingSq_check = lsb(root.pieces[us_check][KING]);
                if (is_square_attacked(root, kingSq_check, root.side)) {
                    unmake_move(root, m, u_check, halfmove_clock);
                    continue;
                }
                unmake_move(root, m, u_check, halfmove_clock);
                

                
                Undo u;
                int hc = halfmove_clock;
                int us = root.side;  // Save side BEFORE make_move
                make_move(root, m, u, hc);
                history.push_back(hash_position(root));
                
                // Check legality: ensure mover's king is NOT left in check
                if (is_our_king_attacked_after_move(root, us)) {
                    history.pop_back();
                    unmake_move(root, m, u, halfmove_clock);
                    continue;
                }
                
                // Check extension - search deeper if in check
                int extension = 0;
                if (is_square_attacked(root, lsb(root.pieces[root.side][KING]), root.side ^ 1)) {
                    extension = 1;
                }
                
                // Move-level futility pruning
                if (d == 1 && legal_moves_count > 0 && extension == 0) {
                    int eval = evaluate(root);
                    if (eval + 200 < alpha && !(root.occ[root.side ^ 1] & bit(m.to))) {
                        history.pop_back();
                        unmake_move(root, m, u, halfmove_clock);
                        continue; // Skip quiet moves that can't improve position
                    }
                }
                
                // SEE Pruning - prune losing captures
                if (d <= 6 && legal_moves_count > 0 && (root.occ[root.side ^ 1] & bit(m.to))) {
                    int see_score = see(root, m);
                    int see_threshold = -50 * d; // More lenient at higher depths
                    
                    if (see_score < see_threshold) {
                        history.pop_back();
                        unmake_move(root, m, u, halfmove_clock);
                        continue; // Skip losing captures
                    }
                }
                
                legal_moves_count++;
                
                int score;
                
                // PVS: Full window for first move, null window for rest
                if (legal_moves_count == 1) {
                    // First move - full window search
                    score = -pvs_search(root, d - 1 + extension, -beta, -alpha, hc, history, 0, true, m);
                } else {
                    // Late Move Reductions
                    int reduced_depth = d - 1 + extension;
                    if (d >= 3 && move_idx >= 3 && !(root.occ[root.side ^ 1] & bit(m.to)) && extension == 0) {
                        int lmr_reduction = 1 + (d >= 5) + (d >= 8);
                        reduced_depth = std::max(1, d - 1 - lmr_reduction);
                    }
                    
                    // Null window search
                    score = -pvs_search(root, reduced_depth, -alpha - 1, -alpha, hc, history, 0, true, m);
                    
                    // Re-search if it failed high
                    if (score > alpha && score < beta) {
                        score = -pvs_search(root, d - 1 + extension, -beta, -alpha, hc, history, 0, true, m);
                    }
                }
                
                history.pop_back();
                unmake_move(root, m, u, halfmove_clock);
                
                if (score > iteration_best) {
                    iteration_best = score;
                    iteration_best_move = m;
                    
                    // Update history heuristic
                    if (!(root.occ[root.side ^ 1] & bit(m.to))) {
                        ::history[root.side][m.from][m.to] += d * d;
                        
                        // Update continuation history - only if previous move is valid (not null move)
                        if (m.from != iteration_best_move.from || m.to != iteration_best_move.to) {
                            int curr_piece = -1; // Initialize to invalid value
                            for (int p = 1; p <= 6; p++) {
                                if (root.pieces[root.side][p] & bit(m.from)) {
                                    curr_piece = p - 1; // 0-indexed
                                    break;
                                }
                            }
                            
                            // Safety check for array bounds - ensure piece is valid and square is valid
                            if (curr_piece >= 0 && curr_piece < 6 && m.to >= 0 && m.to < 64) {
                                continuation_history[root.side][curr_piece][m.to] += d * d;
                                
                                // Prevent overflow
                                if (continuation_history[root.side][curr_piece][m.to] > 10000) {
                                    // Age all continuation history with bounds checking
                                    for (int s = 0; s < 2; s++)
                                        for (int p = 0; p < 6; p++)
                                            for (int sq = 0; sq < 64; sq++)
                                                continuation_history[s][p][sq] /= 2;
                                }
                            }
                        }
                        
                        // Prevent overflow for regular history
                        if (::history[root.side][m.from][m.to] > 10000) {
                            // Age all history scores
                            for (int s = 0; s < 2; s++)
                                for (int f = 0; f < 64; f++)
                                    for (int t = 0; t < 64; t++)
                                        ::history[s][f][t] /= 2;
                        }
                    }
                    g_tt_move = m;
                }
                
                if (score > alpha) alpha = score;
                
                if (alpha >= beta) {
                    // Beta cutoff - update killers and countermove
                    if (!(root.occ[root.side ^ 1] & bit(m.to))) {
                        // Safety check for killers array bounds
                        if (0 < 100) {
                            killers[0][1] = killers[0][0];
                            killers[0][0] = m;
                        }
                        
                        // Update countermove - only if previous move is valid (not null move)
                        if (m.from != iteration_best_move.from || m.to != iteration_best_move.to) {
                            countermoves[m.from][m.to] = iteration_best_move;
                        }
                    }
                    break;
                }
            }
        } while (research);
        
        // Checkmate or stalemate
        if (legal_moves_count == 0) {
            if (is_square_attacked(root, lsb(root.pieces[root.side][KING]), root.side ^ 1))
                return {-30000, 0, 0};
            return {0, 0, 0};
        }
        
        // Update best score and move
        if (iteration_best > bestScore) {
            bestScore = iteration_best;
            best = iteration_best_move;
        }
        
        // Handle aspiration window research
        if (iteration_best <= alpha || iteration_best >= beta) {
            research = true;
            if (iteration_best <= alpha) {
                alpha = std::max(-30000, iteration_best - 25);
            }
            if (iteration_best >= beta) {
                beta = std::min(100000, iteration_best + 25);
            }
        }
        
        // Output UCI info
        auto now = high_resolution_clock::now();
        auto elapsed_ms = duration_cast<milliseconds>(now - start).count();
        std::cout << "info depth " << d
                  << " score cp " << iteration_best
                  << " nodes " << g_nodes_searched.load()
                  << " time " << elapsed_ms
                  << " pv " << move_to_uci(iteration_best_move) << std::endl;
    }
    
    // Final safety check: ensure we have a valid move
    if (best.from == 0 && best.to == 0 && best.promo == 0) {
        // If still no valid move found, try to return the first legal move
        if (!legal_moves_vec.empty()) {
            best = legal_moves_vec[0];
        }
    }

    // Output the required UCI bestmove so GUI receives the result
    std::cout << "bestmove " << move_to_uci(best) << std::endl;
    std::cout.flush();

    return best;
}

// ===============================
// UCI LOOP (FOR INTERFACE)
// ==============================

void uci_loop() {
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream iss(line);
        std::string token;
        iss >> token;
        if (token == "uci") {
            std::cout << "id name Douchess\n";
            std::cout << "id author Doulet Media Developer Team\n";
            std::cout << "uciok\n";
        } else if (token == "isready") {
            std::cout << "readyok\n";
        } else if (token == "position") {
            std::string sub;
            iss >> sub;
            if (sub == "startpos") {
                g_current_position = start_position();
                std::string moves_token;
                if (iss >> moves_token && moves_token == "moves") {
                    std::string move_str;
                    while (iss >> move_str) {
                        Move m = parse_uci_move(g_current_position, move_str);
                        Undo u;
                        int hc = 0;
                        make_move(g_current_position, m, u, hc);
                    }
                }
            } else if (sub == "fen") {
                std::string fen;
                std::string part;
                while (iss >> part && part != "moves") {
                    fen += part + " ";
                }
                g_current_position = fen_to_position(fen);

                if (part == "moves") {
                    std::string move_str;
                    while (iss >> move_str) {
                        Move m = parse_uci_move(g_current_position, move_str);
                        Undo u;
                        int hc = 0;
                        make_move(g_current_position, m, u, hc);
                    }
                }
            }
        } else if (token == "go") {
            int wtime = 0, btime = 0, winc = 0, binc = 0;
            int movetime = 0;  // ADD THIS
            int depth = 99;     // ADD THIS

            std::string t;
            while (iss >> t) {
                if (t == "wtime") iss >> wtime;
                else if (t == "btime") iss >> btime;
                else if (t == "winc") iss >> winc;
                else if (t == "binc") iss >> binc;
                else if (t == "movetime") iss >> movetime;  // ADD THIS
                else if (t == "depth") iss >> depth;        // ADD THIS
            }

            int time_ms = 1000; // Default 1 second

            // If movetime is specified, use it directly
            if (movetime > 0) {
                time_ms = movetime;
            }
            // Otherwise calculate from wtime/btime
            else if (wtime > 0 || btime > 0) {
                int side = g_current_position.side;
                int mytime = (side == WHITE) ? wtime : btime;
                int myinc = (side == WHITE) ? winc : binc;

                if (mytime > 0) {
                    int target = std::max(10, std::min(mytime / 30 + myinc, mytime));
                    time_ms = target;
                }
            }

            // Limit maximum search depth to prevent stack overflow
            if (depth > 30) {
                depth = 30;
            }

            Position p = g_current_position;
            Move m = search_root(p, depth, time_ms);

            // Note: search_root now prints the UCI 'bestmove' line directly before returning.
            // The engine previously printed 'bestmove' here; that output has been centralized to
            // the search_root function to ensure the GUI always receives the final bestmove.

            // No additional printing here; continue the loop
        } else if (token == "stop") {
            g_stop_search.store(true);
        } else if (token == "quit") {
            break;
        }
    }
}

// Initialize search-related arrays
void init_search() {
    // Initialize killers
    for (int i = 0; i < 100; i++) {
        killers[i][0] = {0, 0, 0};
        killers[i][1] = {0, 0, 0};
    }
    
    // Initialize TT move
    g_tt_move = {0, 0, 0};
    
    // Initialize history
    for (int s = 0; s < 2; s++) {
        for (int f = 0; f < 64; f++) {
            for (int t = 0; t < 64; t++) {
                history[s][f][t] = 0;
            }
        }
    }
    
    // Initialize countermoves
    for (int f = 0; f < 64; f++) {
        for (int t = 0; t < 64; t++) {
            countermoves[f][t] = {0, 0, 0};
        }
    }
    
    // Initialize continuation history
    for (int s = 0; s < 2; s++) {
        for (int p = 0; p < 6; p++) {
            for (int sq = 0; sq < 64; sq++) {
                continuation_history[s][p][sq] = 0;
            }
        }
    }
    
    // Initialize TT
    for (int i = 0; i < TT_SIZE; i++) {
        TT[i].key = 0;
        TT[i].depth = 0;
        TT[i].score = 0;
        TT[i].flag = 0;
        TT[i].age = 0;
    }
}

int main() {
    init_magics();
    init_zobrist();
    init_move_tables();
    init_search(); // Initialize all search arrays
    init_eval_params(); // Initialize evaluation parameters for tuning
    
    // initialize global UCI position after start_position is available
    g_current_position = start_position();
    // Enable UCI loop as primary interface
    uci_loop();
    return 0;
}
