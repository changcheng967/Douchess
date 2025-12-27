#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

#include <iostream>
#include <cstdint>
#include <string>
#include <array>
#include <vector>
#include <algorithm>
#include <chrono>
#include <thread>
#include <ctime>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <random>

// ========================================
// INSERT AT TOP: Time Management & Constants (Windows Compatible)
// ========================================
// MVV/LVA [Attacker][Victim]
// MVV/LVA scoring removed - not used in current implementation

// Cross-platform time function
long long current_time_ms() {
#ifdef _WIN32
    // Windows-specific time implementation
    static LARGE_INTEGER frequency;
    static BOOL first = TRUE;
    if (first) {
        QueryPerformanceFrequency(&frequency);
        first = FALSE;
    }
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (now.QuadPart * 1000) / frequency.QuadPart;
#else
    // Linux/Unix implementation
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
#endif
}

// ========================================
// 1. Constants and Enums
// ========================================
typedef uint64_t U64;

// Named constants for better code readability
const int MAX_DEPTH = 64;
const int MAX_PLY = 64;
const int INFINITY_SCORE = 32000;
const int MATE_SCORE = 30000;
const int TIME_LIMIT_MS = 2000;  // 2 seconds instead of 950ms
// NODE_CHECK_INTERVAL removed - no longer used
const int DELTA_PRUNING_MARGIN = 200;
const int FUTILITY_MARGIN = 300;  // Was 200, now more conservative
const int REVERSE_FUTILITY_MARGIN = 100;  // Was 150, now less aggressive
const int HISTORY_MAX = 10000;
const int EVAL_CLAMP_MAX = 5000;
const int EVAL_CLAMP_MIN = -5000;
const int ASPIRATION_WINDOW = 75;  // Wider window, fewer re-searches

// Piece types
enum { P, N, B, R, Q, K };

// Colors
enum { WHITE, BLACK };

// Squares
enum {
    a8, b8, c8, d8, e8, f8, g8, h8,
    a7, b7, c7, d7, e7, f7, g7, h7,
    a6, b6, c6, d6, e6, f6, g6, h6,
    a5, b5, c5, d5, e5, f5, g5, h5,
    a4, b4, c4, d4, e4, f4, g4, h4,
    a3, b3, c3, d3, e3, f3, g3, h3,
    a2, b2, c2, d2, e2, f2, g2, h2,
    a1, b1, c1, d1, e1, f1, g1, h1
};

// ========================================
// 2. Move Structure
// ========================================
struct Move {
    uint32_t move;
    
    Move() : move(0) {}
    Move(int from, int to, int piece, int promo = 0, bool capture = false, bool double_push = false, bool enpassant = false, bool castling = false) {
        move = (from) | (to << 6) | (piece << 12) | (promo << 15) | (capture << 18) | (double_push << 19) | (enpassant << 20) | (castling << 21);
    }
    
    int get_from() const { return move & 0x3F; }
    int get_to() const { return (move >> 6) & 0x3F; }
    int get_piece() const { return (move >> 12) & 0x7; }
    int get_promo() const { return (move >> 15) & 0x7; }
    bool is_capture() const { return (move >> 18) & 1; }
    bool is_double_push() const { return (move >> 19) & 1; }
    bool is_enpassant() const { return (move >> 20) & 1; }
    bool is_castling() const { return (move >> 21) & 1; }
};

// ========================================
// 3. Position and Board Structures
// ========================================
struct Position {
    U64 pieces[2][6];
    U64 occupancies[3];
    int side_to_move;
    int castling_rights;
    int en_passant_square;
    U64 hash_key;
    
    Position() {
        memset(pieces, 0, sizeof(pieces));
        memset(occupancies, 0, sizeof(occupancies));
        side_to_move = WHITE;
        castling_rights = 0;
        en_passant_square = -1;
        hash_key = 0;
    }
};

struct BoardState {
    U64 hash_key;
    int castling_rights;
    int en_passant_square;
    int captured_piece;
    int halfmove_clock;
};


struct MoveList {
    std::vector<Move> moves;
    void add_move(const Move& move) { moves.push_back(move); }
    void clear() { moves.clear(); }
};

// ========================================
// 4. Transposition Table Structures
// ========================================
enum { TT_EXACT, TT_ALPHA, TT_BETA };

struct TTEntry {
    U64 key = 0;
    int depth = 0;
    int flag = 0;
    int score = 0;
    Move move;
};

// ========================================
// 5. Global Variables
// ========================================
// Search control
bool time_up = false;
long long start_time = 0;
long long time_limit = 2000;
long long nodes_searched = 0;

// Game state tracking
std::vector<U64> position_history;
int halfmove_clock = 0;

// Missing variable declarations

// Countermove heuristic - removed due to broken implementation

// Function declarations for tactical analysis
int detect_hanging_pieces(const Position& pos, int color);
int detect_threats(const Position& pos, int color);
int detect_tactical_patterns(const Position& pos, int color);
int detect_trapped_pieces(const Position& pos, int color);

// PV Table (Principal Variation Table)
Move pv_table[MAX_PLY][MAX_PLY];
int pv_length[MAX_PLY];

// Killer Moves & History
Move killer_moves[2][MAX_DEPTH];
int history_moves[6][64];

// Transposition Table
const int TT_SIZE = 1 << 24;  // 16 million entries (~512 MB) - Better for 1 sec/move
TTEntry TTable[TT_SIZE];

// Attack Tables
U64 knight_attacks[64];
U64 king_attacks[64];

// Zobrist Keys
U64 piece_keys[2][6][64];
U64 enpassant_keys[64];
U64 castle_keys[16];
U64 side_key;

// ========================================
// 6. Forward Declarations
// ========================================
bool is_square_attacked(const Position& pos, int square, int side);
void unmake_move(Position& pos, const Move& move, const BoardState& state);
int eval_mobility(const Position& pos, int color);
int eval_king_safety(const Position& pos, int color);
int eval_pawns(const Position& pos);
void generate_pawn_moves(const Position& pos, MoveList& move_list, int color);
void generate_knight_moves(const Position& pos, MoveList& move_list, int color);
void generate_bishop_moves(const Position& pos, MoveList& move_list, int color);
void generate_rook_moves(const Position& pos, MoveList& move_list, int color);
void generate_queen_moves(const Position& pos, MoveList& move_list, int color);
void generate_king_moves(const Position& pos, MoveList& move_list, int color);
void generate_castling_moves(const Position& pos, MoveList& move_list, int color);
MoveList generate_moves(const Position& pos);
void generate_captures(const Position& pos, std::vector<Move>& captures);
MoveList generate_legal_moves(Position& pos);
U64 generate_hash_key(const Position& pos);
void sort_moves_enhanced(const Position& pos, std::vector<Move>& moves, const Move& tt_move, int ply = 0);
uint64_t perft(Position& pos, int depth);
void run_perft_tests();
Move parse_move(Position& pos, const std::string& move_str);
void print_move(const Move& move);
void print_move_list(const MoveList& move_list);
void print_move_uci(int move_int);
void init_zobrist_keys();

void init_zobrist_keys() {
    // Use proper 64-bit Mersenne Twister for high-quality random numbers
    std::mt19937_64 rng(12345);
    
    // Initialize piece keys with proper 64-bit random numbers
    for (int color = 0; color < 2; color++) {
        for (int piece = 0; piece < 6; piece++) {
            for (int square = 0; square < 64; square++) {
                piece_keys[color][piece][square] = rng();
            }
        }
    }
    
    // Initialize enpassant keys
    for (int square = 0; square < 64; square++) {
        enpassant_keys[square] = rng();
    }
    
    // Initialize castle keys
    for (int i = 0; i < 16; i++) {
        castle_keys[i] = rng();
    }
    
    // Initialize side key
    side_key = rng();
}


// ========================================
// 3. Bit Manipulation Functions
// ========================================
#ifdef _MSC_VER
    #include <intrin.h>
    inline int count_bits(U64 bitboard) { return (int)__popcnt64(bitboard); }
    inline int lsb_index(U64 bitboard) {
            // CRITICAL FIX: Bounds checking for empty bitboards
            if (bitboard == 0) return -1;
            unsigned long index;
            if (_BitScanForward64(&index, bitboard)) return index;
            return -1; // Should not happen for valid bitboards
        }
#else
    inline int count_bits(U64 bitboard) { return (int)__builtin_popcountll(bitboard); }
    inline int lsb_index(U64 bitboard) {
        // CRITICAL FIX: Bounds checking for empty bitboards
        if (bitboard == 0) return -1;
        return __builtin_ctzll(bitboard);
    }
#endif

inline int get_bit(U64 bitboard, int square) { return (int)((bitboard >> square) & 1ULL); }
inline void set_bit(U64 &bitboard, int square) { bitboard |= (1ULL << square); }
inline void pop_bit(U64 &bitboard, int square) { int bit = get_bit(bitboard, square); if (bit) bitboard ^= (1ULL << square); }

// ========================================
// 6. Position Structure (Already defined above)
// ========================================

// Forward declarations already done above

// ========================================
// CRITICAL FIX 1: Proper Position Setup
// ========================================

void setup_starting_position(Position& pos) {
    // Clear everything
    memset(&pos, 0, sizeof(Position));
    pos.en_passant_square = -1;
    
    // White pieces (a8=0 coordinate system)
    // Rank 1 = bits 56-63
    pos.pieces[WHITE][R] = (1ULL << 56) | (1ULL << 63);  // a1, h1
    pos.pieces[WHITE][N] = (1ULL << 57) | (1ULL << 62);  // b1, g1
    pos.pieces[WHITE][B] = (1ULL << 58) | (1ULL << 61);  // c1, f1
    pos.pieces[WHITE][Q] = (1ULL << 59);                 // d1
    pos.pieces[WHITE][K] = (1ULL << 60);                 // e1
    
    // Rank 2 = bits 48-55
    pos.pieces[WHITE][P] = 0ULL;
    for (int file = 0; file < 8; file++) {
        pos.pieces[WHITE][P] |= (1ULL << (48 + file));  // a2-h2
    }
    
    // Black pieces (a8=0 coordinate system)
    // Rank 8 = bits 0-7
    pos.pieces[BLACK][R] = (1ULL << 0) | (1ULL << 7);    // a8, h8
    pos.pieces[BLACK][N] = (1ULL << 1) | (1ULL << 6);    // b8, g8
    pos.pieces[BLACK][B] = (1ULL << 2) | (1ULL << 5);    // c8, f8
    pos.pieces[BLACK][Q] = (1ULL << 3);                  // d8
    pos.pieces[BLACK][K] = (1ULL << 4);                  // e8
    
    // Rank 7 = bits 8-15
    pos.pieces[BLACK][P] = 0ULL;
    for (int file = 0; file < 8; file++) {
        pos.pieces[BLACK][P] |= (1ULL << (8 + file));   // a7-h7
    }
    
    // Calculate occupancies
    pos.occupancies[WHITE] = 0ULL;
    pos.occupancies[BLACK] = 0ULL;
    for (int piece = 0; piece < 6; piece++) {
        pos.occupancies[WHITE] |= pos.pieces[WHITE][piece];
        pos.occupancies[BLACK] |= pos.pieces[BLACK][piece];
    }
    pos.occupancies[2] = pos.occupancies[WHITE] | pos.occupancies[BLACK];
    
    // Game state
    pos.side_to_move = WHITE;
    pos.castling_rights = 15; // All castling available (KQkq)
    pos.en_passant_square = -1;
    
    // Generate hash
    pos.hash_key = generate_hash_key(pos);
}

// ========================================
// 7. Attack Table Initialization
// ========================================
void init_knight_attacks() {
    for (int square = 0; square < 64; square++) {
        U64 attacks = 0ULL;
        int rank = square / 8, file = square % 8;
        int offsets[8][2] = {{-2,-1},{-2,1},{-1,-2},{-1,2},{1,-2},{1,2},{2,-1},{2,1}};
        for (int i = 0; i < 8; i++) {
            int new_rank = rank + offsets[i][0], new_file = file + offsets[i][1];
            if (new_rank >= 0 && new_rank < 8 && new_file >= 0 && new_file < 8) {
                int target_square = new_rank * 8 + new_file;
                set_bit(attacks, target_square);
            }
        }
        knight_attacks[square] = attacks;
    }
}

void init_king_attacks() {
    for (int square = 0; square < 64; square++) {
        U64 attacks = 0ULL;
        int rank = square / 8, file = square % 8;
        for (int dr = -1; dr <= 1; dr++) {
            for (int df = -1; df <= 1; df++) {
                if (dr == 0 && df == 0) continue;
                int new_rank = rank + dr, new_file = file + df;
                if (new_rank >= 0 && new_rank < 8 && new_file >= 0 && new_file < 8) {
                    int target_square = new_rank * 8 + new_file;
                    set_bit(attacks, target_square);
                }
            }
        }
        king_attacks[square] = attacks;
    }
}

void init_attack_tables() {
    init_knight_attacks();
    init_king_attacks();
}

// ========================================
// 8. Helper Functions
// ========================================
U64 generate_hash_key(const Position& pos) {
    U64 key = 0ULL;
    for (int color = 0; color < 2; color++) {
        for (int piece = 0; piece < 6; piece++) {
            U64 bitboard = pos.pieces[color][piece];
            while (bitboard) {
                int sq = lsb_index(bitboard);
                pop_bit(bitboard, sq);
                key ^= piece_keys[color][piece][sq];
            }
        }
    }
    if (pos.en_passant_square != -1) {
        key ^= enpassant_keys[pos.en_passant_square];
    }
    key ^= castle_keys[pos.castling_rights];
    if (pos.side_to_move == BLACK) key ^= side_key;
    return key;
}

void print_bitboard(U64 bitboard) {
    for (int rank = 0; rank < 8; rank++) {
        for (int file = 0; file < 8; file++) {
            int square = rank * 8 + file;
            std::cout << (get_bit(bitboard, square) ? "1 " : ". ");
        }
        std::cout << std::endl;
    }
    std::cout << std::endl;
}

std::string square_to_algebraic(int square) {
    // CRITICAL FIX: Convert a8=0 coordinate to UCI a1=0 coordinate
    // In a8=0 system: a8=0, h1=63
    // In UCI a1=0 system: a1=0, h8=63
    // So we need to flip the rank only: file stays same, rank = 7 - current_rank
    char file = 'a' + (square % 8);
    char rank = '8' - (square / 8);  // Flip rank: 0->7, 1->6, ..., 7->0
    return std::string(1, file) + rank;
}

// ========================================
// 9. Move Generation (Sliding Pieces)
// ========================================
U64 get_bishop_attacks(int square, U64 block) {
    U64 attacks = 0ULL;
    int tr = square / 8, tf = square % 8;
    for (int r = tr + 1, f = tf + 1; r <= 7 && f <= 7; r++, f++) { set_bit(attacks, r * 8 + f); if (get_bit(block, r * 8 + f)) break; }
    for (int r = tr - 1, f = tf + 1; r >= 0 && f <= 7; r--, f++) { set_bit(attacks, r * 8 + f); if (get_bit(block, r * 8 + f)) break; }
    for (int r = tr + 1, f = tf - 1; r <= 7 && f >= 0; r++, f--) { set_bit(attacks, r * 8 + f); if (get_bit(block, r * 8 + f)) break; }
    for (int r = tr - 1, f = tf - 1; r >= 0 && f >= 0; r--, f--) { set_bit(attacks, r * 8 + f); if (get_bit(block, r * 8 + f)) break; }
    return attacks;
}

U64 get_rook_attacks(int square, U64 block) {
    U64 attacks = 0ULL;
    int tr = square / 8, tf = square % 8;
    for (int r = tr + 1; r <= 7; r++) { set_bit(attacks, r * 8 + tf); if (get_bit(block, r * 8 + tf)) break; }
    for (int r = tr - 1; r >= 0; r--) { set_bit(attacks, r * 8 + tf); if (get_bit(block, r * 8 + tf)) break; }
    for (int f = tf + 1; f <= 7; f++) { set_bit(attacks, tr * 8 + f); if (get_bit(block, tr * 8 + f)) break; }
    for (int f = tf - 1; f >= 0; f--) { set_bit(attacks, tr * 8 + f); if (get_bit(block, tr * 8 + f)) break; }
    return attacks;
}

U64 get_queen_attacks(int square, U64 block) {
    return get_rook_attacks(square, block) | get_bishop_attacks(square, block);
}

void generate_pawn_moves(const Position& pos, MoveList& move_list, int color) {
    if (color == WHITE) {
        // -------------------------------------------------------------------------
        // WHITE PAWN MOVES (Moving UP -> Decreasing Index)
        // -------------------------------------------------------------------------
        U64 bitboard = pos.pieces[WHITE][P];
        
        while (bitboard) {
            int source_square = lsb_index(bitboard);
            
            // 1. Single Push (Target is UP/Minus 8)
            int target_square = source_square - 8;
            
            // Check bounds (>=0) and emptiness
            if (!(target_square < 0) && !get_bit(pos.occupancies[2], target_square)) {
                
                // PROMOTION (Rank 8 is Indices 0-7)
                if (source_square >= a7 && source_square <= h7) {
                    move_list.add_move(Move(source_square, target_square, P, Q, false));
                    move_list.add_move(Move(source_square, target_square, P, R, false));
                    move_list.add_move(Move(source_square, target_square, P, B, false));
                    move_list.add_move(Move(source_square, target_square, P, N, false));
                }
                else {
                    // Normal Single Push
                    move_list.add_move(Move(source_square, target_square, P, 0, false));
                    
                    // 2. Double Push
                    // White Pawns start on Rank 2 -> Indices 48-55 (a2-h2)
                    if (source_square >= a2 && source_square <= h2) {
                        // Check if square - 16 is empty (target)
                        if (!get_bit(pos.occupancies[2], source_square - 16)) {
                            move_list.add_move(Move(source_square, source_square - 16, P, 0, false, true));
                        }
                    }
                }
            }
            
            // 3. Captures (White Captures Up-Left and Up-Right)
            // Note: In a8=0, Left is -1, Right is +1.
            // Up-Left: -8 -1 = -9
            // Up-Right: -8 +1 = -7
            
            // Capture Left (-9)
            // Ensure not on A file (File 0) prevents wrapping
            if (source_square % 8 != 0) {
                 int capture_target = source_square - 9;
                 if (capture_target >= 0 && get_bit(pos.occupancies[BLACK], capture_target)) {
                     // Add capture moves (include promotion checks similar to above)
                     if (source_square >= a7 && source_square <= h7) {
                         move_list.add_move(Move(source_square, capture_target, P, Q, true));
                         move_list.add_move(Move(source_square, capture_target, P, R, true));
                         move_list.add_move(Move(source_square, capture_target, P, B, true));
                         move_list.add_move(Move(source_square, capture_target, P, N, true));
                     } else {
                         move_list.add_move(Move(source_square, capture_target, P, 0, true));
                     }
                 }
            }
            
            // Capture Right (-7)
            // Ensure not on H file (File 7)
            if (source_square % 8 != 7) {
                 int capture_target = source_square - 7;
                 if (capture_target >= 0 && get_bit(pos.occupancies[BLACK], capture_target)) {
                     if (source_square >= a7 && source_square <= h7) {
                         move_list.add_move(Move(source_square, capture_target, P, Q, true));
                         move_list.add_move(Move(source_square, capture_target, P, R, true));
                         move_list.add_move(Move(source_square, capture_target, P, B, true));
                         move_list.add_move(Move(source_square, capture_target, P, N, true));
                     } else {
                         move_list.add_move(Move(source_square, capture_target, P, 0, true));
                     }
                 }
            }

            pop_bit(bitboard, source_square);
        }
    }
    else {
        // -------------------------------------------------------------------------
        // BLACK PAWN MOVES (Moving DOWN -> Increasing Index)
        // -------------------------------------------------------------------------
        U64 bitboard = pos.pieces[BLACK][P];
        
        while (bitboard) {
            int source_square = lsb_index(bitboard);
            
            // 1. Single Push (Target is DOWN/Plus 8)
            int target_square = source_square + 8;
            
            // Check bounds (<64) and emptiness
            if (target_square < 64 && !get_bit(pos.occupancies[2], target_square)) {
                
                // PROMOTION (Black pawns on rank 2 promote when moving to rank 1)
                if (source_square >= a2 && source_square <= h2) {
                    move_list.add_move(Move(source_square, target_square, P, Q, false));
                    move_list.add_move(Move(source_square, target_square, P, R, false));
                    move_list.add_move(Move(source_square, target_square, P, B, false));
                    move_list.add_move(Move(source_square, target_square, P, N, false));
                }
                else {
                    // Normal Single Push
                    move_list.add_move(Move(source_square, target_square, P, 0, false));
                    
                    // 2. Double Push
                    // Black Pawns start on Rank 7 -> Indices 8-15 (a7-h7)
                    if (source_square >= a7 && source_square <= h7) {
                        // Check if square + 16 is empty (target)
                        if (!get_bit(pos.occupancies[2], source_square + 16)) {
                            move_list.add_move(Move(source_square, source_square + 16, P, 0, false, true));
                        }
                    }
                }
            }
            
            // 3. Captures (Black Captures Down-Left and Down-Right)
            // Down-Left: +8 -1 = +7
            // Down-Right: +8 +1 = +9
            
            // Capture Left (+7)
            // Ensure not on A file (File 0)
            if (source_square % 8 != 0) {
                 int capture_target = source_square + 7;
                 if (capture_target < 64 && get_bit(pos.occupancies[WHITE], capture_target)) {
                     if (source_square >= a2 && source_square <= h2) {
                         move_list.add_move(Move(source_square, capture_target, P, Q, true));
                         move_list.add_move(Move(source_square, capture_target, P, R, true));
                         move_list.add_move(Move(source_square, capture_target, P, B, true));
                         move_list.add_move(Move(source_square, capture_target, P, N, true));
                     } else {
                         move_list.add_move(Move(source_square, capture_target, P, 0, true));
                     }
                 }
            }
            
            // Capture Right (+9)
            // Ensure not on H file (File 7)
            if (source_square % 8 != 7) {
                 int capture_target = source_square + 9;
                 if (capture_target < 64 && get_bit(pos.occupancies[WHITE], capture_target)) {
                     if (source_square >= a2 && source_square <= h2) {
                         move_list.add_move(Move(source_square, capture_target, P, Q, true));
                         move_list.add_move(Move(source_square, capture_target, P, R, true));
                         move_list.add_move(Move(source_square, capture_target, P, B, true));
                         move_list.add_move(Move(source_square, capture_target, P, N, true));
                     } else {
                         move_list.add_move(Move(source_square, capture_target, P, 0, true));
                     }
                 }
            }

            pop_bit(bitboard, source_square);
        }
    }
    
    // -------------------------------------------------------------------------
    // EN PASSANT CAPTURES (Both Colors)
    // -------------------------------------------------------------------------
    if (pos.en_passant_square != -1) {
        int ep_sq = pos.en_passant_square;
        
        if (color == WHITE) {
            // White en passant: white pawn on rank 5 captures black pawn that moved from rank 7 to rank 5
            // If en passant square is e6 (20), white pawns that can capture are at d5 (27) and f5 (29)
            if (ep_sq % 8 != 0) {  // Not on A file
                int left_pawn = ep_sq + 7;  // ✅ e6 (20) + 7 = 27 (d5)
                if (left_pawn < 64 && get_bit(pos.pieces[WHITE][P], left_pawn)) {
                    move_list.add_move(Move(left_pawn, ep_sq, P, 0, true, false, true, false));
                }
            }
            if (ep_sq % 8 != 7) {  // Not on H file
                int right_pawn = ep_sq + 9;  // ✅ e6 (20) + 9 = 29 (f5)
                if (right_pawn < 64 && get_bit(pos.pieces[WHITE][P], right_pawn)) {
                    move_list.add_move(Move(right_pawn, ep_sq, P, 0, true, false, true, false));
                }
            }
        }
        else {
            // Black en passant: black pawn on rank 4 captures white pawn that moved from rank 2 to rank 4
            // If en passant square is e3 (44), black pawns that can capture are at d4 (37) and f4 (39)
            if (ep_sq % 8 != 0) {  // Not on A file
                int left_pawn = ep_sq - 7;  // e3 - 7 = d4
                if (left_pawn >= 0 && get_bit(pos.pieces[BLACK][P], left_pawn)) {
                    move_list.add_move(Move(left_pawn, ep_sq, P, 0, true, false, true, false));
                }
            }
            if (ep_sq % 8 != 7) {  // Not on H file
                int right_pawn = ep_sq - 9;  // e3 - 9 = f4
                if (right_pawn >= 0 && get_bit(pos.pieces[BLACK][P], right_pawn)) {
                    move_list.add_move(Move(right_pawn, ep_sq, P, 0, true, false, true, false));
                }
            }
        }
    }
}

void generate_knight_moves(const Position& pos, MoveList& move_list, int color) {
    U64 knights = pos.pieces[color][N];
    U64 enemy_occupancy = pos.occupancies[1 - color];
    U64 own_occupancy = pos.occupancies[color];
    
    while (knights) {
        int from = lsb_index(knights);
        pop_bit(knights, from);
        U64 attacks = knight_attacks[from] & ~own_occupancy;
        while (attacks) {
            int to = lsb_index(attacks);
            pop_bit(attacks, to);
            bool capture = get_bit(enemy_occupancy, to);
            move_list.add_move(Move(from, to, N, 0, capture));
        }
    }
}

void generate_bishop_moves(const Position& pos, MoveList& move_list, int color) {
    U64 bishops = pos.pieces[color][B];
    U64 enemy_occupancy = pos.occupancies[1 - color];
    U64 all_occupancy = pos.occupancies[2];
    
    while (bishops) {
        int from = lsb_index(bishops);
        pop_bit(bishops, from);
        U64 attacks = get_bishop_attacks(from, all_occupancy) & ~pos.occupancies[color];
        while (attacks) {
            int to = lsb_index(attacks);
            pop_bit(attacks, to);
            bool capture = get_bit(enemy_occupancy, to);
            move_list.add_move(Move(from, to, B, 0, capture));
        }
    }
}

void generate_rook_moves(const Position& pos, MoveList& move_list, int color) {
    U64 rooks = pos.pieces[color][R];
    U64 enemy_occupancy = pos.occupancies[1 - color];
    U64 all_occupancy = pos.occupancies[2];
    
    while (rooks) {
        int from = lsb_index(rooks);
        pop_bit(rooks, from);
        U64 attacks = get_rook_attacks(from, all_occupancy) & ~pos.occupancies[color];
        while (attacks) {
            int to = lsb_index(attacks);
            pop_bit(attacks, to);
            bool capture = get_bit(enemy_occupancy, to);
            move_list.add_move(Move(from, to, R, 0, capture));
        }
    }
}

void generate_queen_moves(const Position& pos, MoveList& move_list, int color) {
    U64 queens = pos.pieces[color][Q];
    U64 enemy_occupancy = pos.occupancies[1 - color];
    U64 all_occupancy = pos.occupancies[2];
    
    while (queens) {
        int from = lsb_index(queens);
        pop_bit(queens, from);
        U64 attacks = get_queen_attacks(from, all_occupancy) & ~pos.occupancies[color];
        while (attacks) {
            int to = lsb_index(attacks);
            pop_bit(attacks, to);
            bool capture = get_bit(enemy_occupancy, to);
            move_list.add_move(Move(from, to, Q, 0, capture));
        }
    }
}

void generate_king_moves(const Position& pos, MoveList& move_list, int color) {
    U64 kings = pos.pieces[color][K];
    U64 enemy_occupancy = pos.occupancies[1 - color];
    U64 own_occupancy = pos.occupancies[color];
    
    while (kings) {
        int from = lsb_index(kings);
        pop_bit(kings, from);
        U64 attacks = king_attacks[from] & ~own_occupancy;
        while (attacks) {
            int to = lsb_index(attacks);
            pop_bit(attacks, to);
            bool capture = get_bit(enemy_occupancy, to);
            move_list.add_move(Move(from, to, K, 0, capture));
        }
    }
}

// ADD THIS function:
void generate_castling_moves(const Position& pos, MoveList& move_list, int color) {
    if (color == WHITE) {
        // Kingside castling (e1-g1)
        if ((pos.castling_rights & 1) &&
            !get_bit(pos.occupancies[2], f1) &&
            !get_bit(pos.occupancies[2], g1) &&
            !is_square_attacked(pos, e1, BLACK) &&
            !is_square_attacked(pos, f1, BLACK) &&
            !is_square_attacked(pos, g1, BLACK)) {
            move_list.add_move(Move(e1, g1, K, 0, false, false, false, true));
        }
        
        // Queenside castling (e1-c1)
        if ((pos.castling_rights & 2) &&
            !get_bit(pos.occupancies[2], d1) &&
            !get_bit(pos.occupancies[2], c1) &&
            !get_bit(pos.occupancies[2], b1) &&
            !is_square_attacked(pos, e1, BLACK) &&
            !is_square_attacked(pos, d1, BLACK) &&
            !is_square_attacked(pos, c1, BLACK)) {
            move_list.add_move(Move(e1, c1, K, 0, false, false, false, true));
        }
    } else {
        // Black castling (similar logic for e8-g8 and e8-c8)
        if ((pos.castling_rights & 4) &&
            !get_bit(pos.occupancies[2], f8) &&
            !get_bit(pos.occupancies[2], g8) &&
            !is_square_attacked(pos, e8, WHITE) &&
            !is_square_attacked(pos, f8, WHITE) &&
            !is_square_attacked(pos, g8, WHITE)) {
            move_list.add_move(Move(e8, g8, K, 0, false, false, false, true));
        }
        
        if ((pos.castling_rights & 8) &&
            !get_bit(pos.occupancies[2], d8) &&
            !get_bit(pos.occupancies[2], c8) &&
            !get_bit(pos.occupancies[2], b8) &&
            !is_square_attacked(pos, e8, WHITE) &&
            !is_square_attacked(pos, d8, WHITE) &&
            !is_square_attacked(pos, c8, WHITE)) {
            move_list.add_move(Move(e8, c8, K, 0, false, false, false, true));
        }
    }
}

// ADD THIS function:
bool is_square_attacked(const Position& pos, int square, int side);

// ADD THIS function:
BoardState make_move(Position& pos, const Move& move);

MoveList generate_legal_moves(Position& pos) {
    MoveList all_moves = generate_moves(pos);
    MoveList legal_moves;
    
    int our_color = pos.side_to_move;
    U64 king_bb = pos.pieces[our_color][K];
    
    // If no king, all moves are "legal" (shouldn't happen in real games)
    if (king_bb == 0) {
        return all_moves;
    }
    
    for (const auto& move : all_moves.moves) {
        BoardState state = make_move(pos, move);
        
        // ✅ CHECK IF KING EXISTS FIRST!
        U64 king_bb_after = pos.pieces[our_color][K];
        if (king_bb_after == 0) {
            // King was captured - illegal move!
            unmake_move(pos, move, state);
            continue;
        }
        
        int king_square = lsb_index(king_bb_after);
        
        // ✅ Additional safety check
        if (king_square < 0 || king_square >= 64) {
            unmake_move(pos, move, state);
            continue;
        }
        
        // Check if our king is attacked by the opponent
        bool legal = !is_square_attacked(pos, king_square, 1 - our_color);
        
        unmake_move(pos, move, state);
        
        if (legal) {
            legal_moves.add_move(move);
        }
    }
    
    return legal_moves;
}

MoveList generate_moves(const Position& pos) {
    MoveList move_list;
    
    // DEBUG: Count moves by type
    size_t pawn_count = move_list.moves.size();
    generate_pawn_moves(pos, move_list, pos.side_to_move);
    size_t pawns_generated = move_list.moves.size() - pawn_count;
    
    size_t knight_count = move_list.moves.size();
    generate_knight_moves(pos, move_list, pos.side_to_move);
    size_t knights_generated = move_list.moves.size() - knight_count;
    
    size_t bishop_count = move_list.moves.size();
    generate_bishop_moves(pos, move_list, pos.side_to_move);
    size_t bishops_generated = move_list.moves.size() - bishop_count;
    
    size_t rook_count = move_list.moves.size();
    generate_rook_moves(pos, move_list, pos.side_to_move);
    size_t rooks_generated = move_list.moves.size() - rook_count;
    
    size_t queen_count = move_list.moves.size();
    generate_queen_moves(pos, move_list, pos.side_to_move);
    size_t queens_generated = move_list.moves.size() - queen_count;
    
    size_t king_count = move_list.moves.size();
    generate_king_moves(pos, move_list, pos.side_to_move);
    size_t kings_generated = move_list.moves.size() - king_count;
    
    size_t castling_count = move_list.moves.size();
    generate_castling_moves(pos, move_list, pos.side_to_move);
    size_t castlings_generated = move_list.moves.size() - castling_count;
    
    // Debug output removed from production code
    
    return move_list;
}

// Generate only captures and promotions for quiescence search
void generate_captures(const Position& pos, std::vector<Move>& captures) {
    int color = pos.side_to_move;
    int enemy = 1 - color;
    
    // Pawn captures
    U64 pawns = pos.pieces[color][P];
    while (pawns) {
        int from = lsb_index(pawns);
        pop_bit(pawns, from);
        
        // Check for captures
        if (color == WHITE) {
            // White pawn captures
            if (from % 8 != 0) { // Not on A file
                int to = from - 9; // Capture left
                if (to >= 0 && get_bit(pos.occupancies[enemy], to)) {
                    if (from >= a7 && from <= h7) {
                        // Promotion captures
                        captures.push_back(Move(from, to, P, Q, true));
                        captures.push_back(Move(from, to, P, R, true));
                        captures.push_back(Move(from, to, P, B, true));
                        captures.push_back(Move(from, to, P, N, true));
                    } else {
                        captures.push_back(Move(from, to, P, 0, true));
                    }
                }
            }
            if (from % 8 != 7) { // Not on H file
                int to = from - 7; // Capture right
                if (to >= 0 && get_bit(pos.occupancies[enemy], to)) {
                    if (from >= a7 && from <= h7) {
                        captures.push_back(Move(from, to, P, Q, true));
                        captures.push_back(Move(from, to, P, R, true));
                        captures.push_back(Move(from, to, P, B, true));
                        captures.push_back(Move(from, to, P, N, true));
                    } else {
                        captures.push_back(Move(from, to, P, 0, true));
                    }
                }
            }
        } else {
            // Black pawn captures
            if (from % 8 != 0) { // Not on A file
                int to = from + 7; // Capture left
                if (to < 64 && get_bit(pos.occupancies[enemy], to)) {
                    if (from >= a2 && from <= h2) {
                        // Promotion captures
                        captures.push_back(Move(from, to, P, Q, true));
                        captures.push_back(Move(from, to, P, R, true));
                        captures.push_back(Move(from, to, P, B, true));
                        captures.push_back(Move(from, to, P, N, true));
                    } else {
                        captures.push_back(Move(from, to, P, 0, true));
                    }
                }
            }
            if (from % 8 != 7) { // Not on H file
                int to = from + 9; // Capture right
                if (to < 64 && get_bit(pos.occupancies[enemy], to)) {
                    if (from >= a2 && from <= h2) {
                        captures.push_back(Move(from, to, P, Q, true));
                        captures.push_back(Move(from, to, P, R, true));
                        captures.push_back(Move(from, to, P, B, true));
                        captures.push_back(Move(from, to, P, N, true));
                    } else {
                        captures.push_back(Move(from, to, P, 0, true));
                    }
                }
            }
        }
    }
    
    // En passant captures (MOVED OUTSIDE THE LOOP!)
    if (pos.en_passant_square != -1) {
        int ep_sq = pos.en_passant_square;
        if (color == WHITE) {
            // Check if there's a pawn at ep_sq + 7
            if (ep_sq % 8 != 0 && get_bit(pos.pieces[WHITE][P], ep_sq + 7)) {
                captures.push_back(Move(ep_sq + 7, ep_sq, P, 0, true, false, true, false));
            }
            // Check if there's a pawn at ep_sq + 9
            if (ep_sq % 8 != 7 && get_bit(pos.pieces[WHITE][P], ep_sq + 9)) {
                captures.push_back(Move(ep_sq + 9, ep_sq, P, 0, true, false, true, false));
            }
        } else {
            // Black en passant
            if (ep_sq % 8 != 0 && get_bit(pos.pieces[BLACK][P], ep_sq - 9)) {
                captures.push_back(Move(ep_sq - 9, ep_sq, P, 0, true, false, true, false));
            }
            if (ep_sq % 8 != 7 && get_bit(pos.pieces[BLACK][P], ep_sq - 7)) {
                captures.push_back(Move(ep_sq - 7, ep_sq, P, 0, true, false, true, false));
            }
        }
    }
    
    // Knight captures
    U64 knights = pos.pieces[color][N];
    while (knights) {
        int from = lsb_index(knights);
        pop_bit(knights, from);
        U64 attacks = knight_attacks[from] & pos.occupancies[enemy];
        while (attacks) {
            int to = lsb_index(attacks);
            pop_bit(attacks, to);
            captures.push_back(Move(from, to, N, 0, true));
        }
    }
    
    // Bishop captures
    U64 bishops = pos.pieces[color][B];
    U64 occ = pos.occupancies[2];
    while (bishops) {
        int from = lsb_index(bishops);
        pop_bit(bishops, from);
        U64 attacks = get_bishop_attacks(from, occ) & pos.occupancies[enemy];
        while (attacks) {
            int to = lsb_index(attacks);
            pop_bit(attacks, to);
            captures.push_back(Move(from, to, B, 0, true));
        }
    }
    
    // Rook captures
    U64 rooks = pos.pieces[color][R];
    while (rooks) {
        int from = lsb_index(rooks);
        pop_bit(rooks, from);
        U64 attacks = get_rook_attacks(from, occ) & pos.occupancies[enemy];
        while (attacks) {
            int to = lsb_index(attacks);
            pop_bit(attacks, to);
            captures.push_back(Move(from, to, R, 0, true));
        }
    }
    
    // Queen captures
    U64 queens = pos.pieces[color][Q];
    while (queens) {
        int from = lsb_index(queens);
        pop_bit(queens, from);
        U64 attacks = get_queen_attacks(from, occ) & pos.occupancies[enemy];
        while (attacks) {
            int to = lsb_index(attacks);
            pop_bit(attacks, to);
            captures.push_back(Move(from, to, Q, 0, true));
        }
    }
    
    // King captures
    U64 kings = pos.pieces[color][K];
    while (kings) {
        int from = lsb_index(kings);
        pop_bit(kings, from);
        U64 attacks = king_attacks[from] & pos.occupancies[enemy];
        while (attacks) {
            int to = lsb_index(attacks);
            pop_bit(attacks, to);
            captures.push_back(Move(from, to, K, 0, true));
        }
    }
    
    // Promotion pushes (non-capture promotions)
    U64 promo_pawns = pos.pieces[color][P];  // Use a NEW variable!
    while (promo_pawns) {
        int from = lsb_index(promo_pawns);
        pop_bit(promo_pawns, from);
        
        int to;
        if (color == WHITE) {
            to = from - 8;
            if (from >= a7 && from <= h7 && to >= 0 && !get_bit(pos.occupancies[2], to)) {
                captures.push_back(Move(from, to, P, Q, false));
                captures.push_back(Move(from, to, P, R, false));
                captures.push_back(Move(from, to, P, B, false));
                captures.push_back(Move(from, to, P, N, false));
            }
        } else {
            to = from + 8;
            if (from >= a2 && from <= h2 && to < 64 && !get_bit(pos.occupancies[2], to)) {
                captures.push_back(Move(from, to, P, Q, false));
                captures.push_back(Move(from, to, P, R, false));
                captures.push_back(Move(from, to, P, B, false));
                captures.push_back(Move(from, to, P, N, false));
            }
        }
    }
}

// ========================================
// 10. Make/Unmake Move (FIXED)
// ========================================

BoardState make_move(Position& pos, const Move& move) {
    BoardState state;
    state.hash_key = pos.hash_key;
    state.castling_rights = pos.castling_rights;
    state.en_passant_square = pos.en_passant_square;
    state.captured_piece = -1;
    state.halfmove_clock = halfmove_clock;  // SAVE IT!
    
    int from = move.get_from();
    int to = move.get_to();
    int piece = move.get_piece();
    int color = pos.side_to_move;
    int enemy_color = 1 - color;
    
    // CRITICAL: Bounds check for move coordinates
    if (from < 0 || from > 63 || to < 0 || to > 63) {
        std::cerr << "CRITICAL ERROR: Invalid move coords " << from << "->" << to << std::endl;
        exit(1);
    }
    
    // Update hash key - remove piece from from-square
    pos.hash_key ^= piece_keys[color][piece][from];
    
    // Remove piece from from-square
    pop_bit(pos.pieces[color][piece], from);
    pop_bit(pos.occupancies[color], from);
    pop_bit(pos.occupancies[2], from);
    
    // Hash verification removed from production code
    
    // Handle captures
    if (move.is_capture()) {
        for (int p = 0; p < 6; p++) {
            if (get_bit(pos.pieces[enemy_color][p], to)) {
                state.captured_piece = p;
                pos.hash_key ^= piece_keys[enemy_color][p][to];
                pop_bit(pos.pieces[enemy_color][p], to);
                pop_bit(pos.occupancies[enemy_color], to);
                pop_bit(pos.occupancies[2], to);
                
                // FIX: Update castling rights when rook is captured
                if (p == R) {
                    pos.hash_key ^= castle_keys[pos.castling_rights];
                    if (to == a1) pos.castling_rights &= ~2;  // White queenside
                    if (to == h1) pos.castling_rights &= ~1;  // White kingside
                    if (to == a8) pos.castling_rights &= ~8;  // Black queenside
                    if (to == h8) pos.castling_rights &= ~4;  // Black kingside
                    pos.hash_key ^= castle_keys[pos.castling_rights];
                }
                break;
            }
        }
    }
    
    // Handle en passant capture
    if (move.is_enpassant()) {
        int ep_target = to + (color == WHITE ? 8 : -8);
        for (int p = 0; p < 6; p++) {
            if (get_bit(pos.pieces[enemy_color][p], ep_target)) {
                state.captured_piece = p;
                pos.hash_key ^= piece_keys[enemy_color][p][ep_target];
                pop_bit(pos.pieces[enemy_color][p], ep_target);
                pop_bit(pos.occupancies[enemy_color], ep_target);
                pop_bit(pos.occupancies[2], ep_target);
                break;
            }
        }
    }
    
    // Handle castling
    if (move.is_castling()) {
        // Move the rook
        if (to == g1) { // White kingside
            pop_bit(pos.pieces[WHITE][R], h1);
            pop_bit(pos.occupancies[WHITE], h1);
            pop_bit(pos.occupancies[2], h1);
            set_bit(pos.pieces[WHITE][R], f1);
            set_bit(pos.occupancies[WHITE], f1);
            set_bit(pos.occupancies[2], f1);
            pos.hash_key ^= piece_keys[WHITE][R][h1] ^ piece_keys[WHITE][R][f1];
        } else if (to == c1) { // White queenside
            pop_bit(pos.pieces[WHITE][R], a1);
            pop_bit(pos.occupancies[WHITE], a1);
            pop_bit(pos.occupancies[2], a1);
            set_bit(pos.pieces[WHITE][R], d1);
            set_bit(pos.occupancies[WHITE], d1);
            set_bit(pos.occupancies[2], d1);
            pos.hash_key ^= piece_keys[WHITE][R][a1] ^ piece_keys[WHITE][R][d1];
        } else if (to == g8) { // Black kingside
            pop_bit(pos.pieces[BLACK][R], h8);
            pop_bit(pos.occupancies[BLACK], h8);
            pop_bit(pos.occupancies[2], h8);
            set_bit(pos.pieces[BLACK][R], f8);
            set_bit(pos.occupancies[BLACK], f8);
            set_bit(pos.occupancies[2], f8);
            pos.hash_key ^= piece_keys[BLACK][R][h8] ^ piece_keys[BLACK][R][f8];
        } else if (to == c8) { // Black queenside
            pop_bit(pos.pieces[BLACK][R], a8);
            pop_bit(pos.occupancies[BLACK], a8);
            pop_bit(pos.occupancies[2], a8);
            set_bit(pos.pieces[BLACK][R], d8);
            set_bit(pos.occupancies[BLACK], d8);
            set_bit(pos.occupancies[2], d8);
            pos.hash_key ^= piece_keys[BLACK][R][a8] ^ piece_keys[BLACK][R][d8];
        }
    }
    
    // Place piece on to-square
    set_bit(pos.pieces[color][piece], to);
    set_bit(pos.occupancies[color], to);
    set_bit(pos.occupancies[2], to);
    
    // Update hash key - place piece on to-square
    pos.hash_key ^= piece_keys[color][piece][to];
    
    // Handle promotions
    if (move.get_promo() != 0) {
        pos.hash_key ^= piece_keys[color][P][to];
        pos.hash_key ^= piece_keys[color][move.get_promo()][to];
        pop_bit(pos.pieces[color][P], to);
        set_bit(pos.pieces[color][move.get_promo()], to);
    }
    
    // Update castling rights when king/rook moves
    if (piece == K) {
        if (color == WHITE) {
            pos.hash_key ^= castle_keys[pos.castling_rights];
            pos.castling_rights &= ~3; // Clear white castling
            pos.hash_key ^= castle_keys[pos.castling_rights];
        } else {
            pos.hash_key ^= castle_keys[pos.castling_rights];
            pos.castling_rights &= ~12; // Clear black castling
            pos.hash_key ^= castle_keys[pos.castling_rights];
        }
    }
    
    if (piece == R) {
        pos.hash_key ^= castle_keys[pos.castling_rights];
        if (from == a1) pos.castling_rights &= ~2;
        if (from == h1) pos.castling_rights &= ~1;
        if (from == a8) pos.castling_rights &= ~8;
        if (from == h8) pos.castling_rights &= ~4;
        pos.hash_key ^= castle_keys[pos.castling_rights];
    }
    
    // Clear old en passant square from hash
    if (pos.en_passant_square >= 0 && pos.en_passant_square < 64) {
        pos.hash_key ^= enpassant_keys[pos.en_passant_square];
    }
    
    // Handle double pawn push (set new en passant target)
    if (move.is_double_push()) {
        // CRITICAL FIX: Calculate en passant square correctly for a8=0 system
        // White double push: from e2 (52) to e4 (36), en passant should be e3 (44)
        // Black double push: from e7 (12) to e5 (28), en passant should be e6 (20)
        pos.en_passant_square = (from + to) / 2;
        if (pos.en_passant_square >= 0 && pos.en_passant_square < 64) {
            pos.hash_key ^= enpassant_keys[pos.en_passant_square];
        }
    } else {
        pos.en_passant_square = -1;
    }
    
    // Update halfmove clock for 50-move rule
    if (move.is_capture() || move.get_piece() == P) {
        halfmove_clock = 0; // Reset on capture or pawn move
    } else {
        halfmove_clock++; // Increment otherwise
    }
    
    // Switch side to move
    pos.side_to_move = enemy_color;
    pos.hash_key ^= side_key;
    
    // Add current position to history for repetition detection
    position_history.push_back(pos.hash_key);
    
    return state;
}

void unmake_move(Position& pos, const Move& move, const BoardState& state) {
    int from = move.get_from();
    int to = move.get_to();
    int piece = move.get_piece();
    int color = 1 - pos.side_to_move;
    int enemy_color = pos.side_to_move;
    
    pos.side_to_move = color;
    
    // Handle castling - move rook back first
    if (move.is_castling()) {
        if (to == g1) { // White kingside
            pop_bit(pos.pieces[WHITE][R], f1);
            pop_bit(pos.occupancies[WHITE], f1);
            pop_bit(pos.occupancies[2], f1);
            set_bit(pos.pieces[WHITE][R], h1);
            set_bit(pos.occupancies[WHITE], h1);
            set_bit(pos.occupancies[2], h1);
        } else if (to == c1) { // White queenside
            pop_bit(pos.pieces[WHITE][R], d1);
            pop_bit(pos.occupancies[WHITE], d1);
            pop_bit(pos.occupancies[2], d1);
            set_bit(pos.pieces[WHITE][R], a1);
            set_bit(pos.occupancies[WHITE], a1);
            set_bit(pos.occupancies[2], a1);
        } else if (to == g8) { // Black kingside
            pop_bit(pos.pieces[BLACK][R], f8);
            pop_bit(pos.occupancies[BLACK], f8);
            pop_bit(pos.occupancies[2], f8);
            set_bit(pos.pieces[BLACK][R], h8);
            set_bit(pos.occupancies[BLACK], h8);
            set_bit(pos.occupancies[2], h8);
        } else if (to == c8) { // Black queenside
            pop_bit(pos.pieces[BLACK][R], d8);
            pop_bit(pos.occupancies[BLACK], d8);
            pop_bit(pos.occupancies[2], d8);
            set_bit(pos.pieces[BLACK][R], a8);
            set_bit(pos.occupancies[BLACK], a8);
            set_bit(pos.occupancies[2], a8);
        }
    }
    
    // ✅ FIX: Handle promotion BEFORE moving piece back
    if (move.get_promo() != 0) {
        // Remove promoted piece from destination (e.g., queen at e8)
        pop_bit(pos.pieces[color][move.get_promo()], to);
        pop_bit(pos.occupancies[color], to);
        pop_bit(pos.occupancies[2], to);
    } else {
        // Normal piece - remove from destination
        pop_bit(pos.pieces[color][piece], to);
        pop_bit(pos.occupancies[color], to);
        pop_bit(pos.occupancies[2], to);
    }
    
    // Handle en passant capture - restore captured pawn
    if (move.is_enpassant() && state.captured_piece != -1) {
        int ep_target = to + (color == WHITE ? 8 : -8);
        set_bit(pos.pieces[enemy_color][state.captured_piece], ep_target);
        set_bit(pos.occupancies[enemy_color], ep_target);
        set_bit(pos.occupancies[2], ep_target);
    }
    // Handle regular capture
    else if (move.is_capture() && state.captured_piece != -1) {
        set_bit(pos.pieces[enemy_color][state.captured_piece], to);
        set_bit(pos.occupancies[enemy_color], to);
        set_bit(pos.occupancies[2], to);
    }
    
    // Place piece back at origin
    set_bit(pos.pieces[color][piece], from);
    set_bit(pos.occupancies[color], from);
    set_bit(pos.occupancies[2], from);
    
    // Handle promotion - remove promoted piece, restore pawn
    // ✅ REMOVED: Duplicate promotion handling (already done above at lines 1284-1295)
    
    // Restore game state
    pos.en_passant_square = state.en_passant_square;
    pos.castling_rights = state.castling_rights;
    pos.hash_key = state.hash_key;
    
    // Restore halfmove clock
    halfmove_clock = state.halfmove_clock;  // RESTORE IT!
    
    // Remove from position history (for repetition detection)
    if (!position_history.empty()) {
        position_history.pop_back();
    }
}

// ========================================
// 11. Perft Testing (Move Generation Verification)
// ========================================

uint64_t perft(Position& pos, int depth) {
    if (depth == 0) {
        return 1;
    }
    
    uint64_t nodes = 0;
    MoveList moves = generate_legal_moves(pos);  // Use legal moves!
    
    for (const auto& move : moves.moves) {
        BoardState state = make_move(pos, move);
        nodes += perft(pos, depth - 1);
        unmake_move(pos, move, state);
    }
    
    return nodes;
}

// Perft test function for standard positions
void run_perft_tests() {
    std::cout << "\n=== PERFT TESTS ===" << std::endl;
    
    // Test position: Starting position
    Position start_pos;
    setup_starting_position(start_pos);
    
    std::cout << "Starting position perft results:" << std::endl;
    for (int depth = 1; depth <= 4; depth++) {
        uint64_t nodes = perft(start_pos, depth);
        std::cout << "Depth " << depth << ": " << nodes << " nodes" << std::endl;
    }
    
    // FIX: Call perft tests to validate move generation
    // Note: Uncomment the line below to run perft tests
    // run_perft_tests();
}

// ========================================
// 12. Piece-Square Tables (Enhanced Evaluation)
// ========================================

// Piece values (centipawns)
const int piece_values[6] = {100, 320, 333, 500, 900, 20000}; // P, N, B, R, Q, K - Tuned values

// Pawn tables - encourage advancing pawns
static const int pawn_table[64] = {
    0,  0,  0,  0,  0,  0,  0,  0,
    5, 10, 10,-20,-20, 10, 10,  5,
    5, -5,-10,  0,  0,-10, -5,  5,
    0,  0,  0, 20, 20,  0,  0,  0,
    5,  5, 10, 25, 25, 10,  5,  5,
   10, 10, 20, 30, 30, 20, 10, 10,
   50, 50, 50, 50, 50, 50, 50, 50,
    0,  0,  0,  0,  0,  0,  0,  0
};

// Knight tables - encourage centralization
static const int knight_table[64] = {
   -50,-40,-30,-30,-30,-30,-40,-50,
   -40,-20,  0,  5,  5,  0,-20,-40,
   -30,  5, 10, 15, 15, 10,  5,-30,
   -30,  0, 15, 20, 20, 15,  0,-30,
   -30,  5, 15, 20, 20, 15,  5,-30,
   -30,  0, 10, 15, 15, 10,  0,-30,
   -40,-20,  0,  0,  0,  0,-20,-40,
   -50,-40,-30,-30,-30,-30,-40,-50
};

// Bishop tables - encourage diagonals and center
static const int bishop_table[64] = {
   -20,-10,-10,-10,-10,-10,-10,-20,
   -10,  5,  0,  0,  0,  0,  5,-10,
   -10, 10, 10, 10, 10, 10, 10,-10,
   -10,  0, 10, 10, 10, 10,  0,-10,
   -10,  5,  5, 10, 10,  5,  5,-10,
   -10,  0,  5, 10, 10,  5,  0,-10,
   -10,  0,  0, 10, 10,  0,  0,-10,
   -20,-10,-10, -5, -5,-10,-10,-20
};

// Rook tables - encourage open files and 7th rank
static const int rook_table[64] = {
    0,  0,  0,  5,  5,  0,  0,  0,
   -5,  0,  0,  0,  0,  0,  0, -5,
   -5,  0,  0,  0,  0,  0,  0, -5,
   -5,  0,  0,  0,  0,  0,  0, -5,
   -5,  0,  0,  0,  0,  0,  0, -5,
   -5,  0,  0,  0,  0,  0,  0, -5,
    5, 10, 10, 10, 10, 10, 10,  5,
    0,  0,  0,  0,  0,  0,  0,  0
};

// Queen tables - encourage center and mobility
static const int queen_table[64] = {
   -20,-10,-10, -5, -5,-10,-10,-20,
   -10,  0,  0,  0,  0,  0,  0,-10,
   -10,  0,  5,  5,  5,  5,  0,-10,
    -5,  0,  5,  5,  5,  5,  0, -5,
     -5,  0,  5,  5,  5,  5,  0, -5,
   -10,  0,  5,  5,  5,  5,  0,-10,
   -10,  0,  0,  0,  0,  0,  0,-10,
   -20,-10,-10, -5, -5,-10,-10,-20
};

// King tables - encourage safety in opening, activity in endgame
static const int king_table[64] = {
   -30,-40,-40,-50,-50,-40,-40,-30,
   -30,-40,-40,-50,-50,-40,-40,-30,
   -30,-40,-40,-50,-50,-40,-40,-30,
   -30,-40,-40,-50,-50,-40,-40,-30,
   -20,-30,-30,-40,-40,-30,-30,-20,
   -10,-20,-20,-20,-20,-20,-20,-10,
    20, 20,  0,  0,  0,  0, 20, 20,
    20, 30, 10,  0,  0, 10, 30, 20
};


// ========================================
// FORWARD DECLARATIONS
// ========================================
int eval_pawns(const Position& pos);
int eval_king_safety(const Position& pos, int color);
int eval_mobility(const Position& pos, int color);
int eval_development(const Position& pos, int color);

// ========================================
// INSERT: Tapered Evaluation (Middlegame + Endgame)
// ========================================
const int mg_pawn_table[64] = {
    0,  0,  0,  0,  0,  0,  0,  0,
    5, 10, 10,-20,-20, 10, 10,  5,
    5, -5,-10,  0,  0,-10, -5,  5,
    0,  0,  0, 20, 20,  0,  0,  0,
    5,  5, 10, 25, 25, 10,  5,  5,
    10, 10, 20, 30, 30, 20, 10, 10,
    50, 50, 50, 50, 50, 50, 50, 50,
    0,  0,  0,  0,  0,  0,  0,  0
};

const int eg_pawn_table[64] = {
    0,  0,  0,  0,  0,  0,  0,  0,
    10, 20, 20, 30, 30, 20, 20, 10,
    10, 10, 20, 30, 30, 20, 10, 10,
    20, 20, 30, 40, 40, 30, 20, 20,
    30, 30, 40, 50, 50, 40, 30, 30,
    40, 40, 50, 60, 60, 50, 40, 40,
    80, 80, 80, 80, 80, 80, 80, 80,
    0,  0,  0,  0,  0,  0,  0,  0
};

const int mg_king_table[64] = {
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -20,-30,-30,-40,-40,-30,-30,-20,
    -10,-20,-20,-20,-20,-20,-20,-10,
     20, 20,  0,  0,  0,  0, 20, 20,
     20, 30, 10,  0,  0, 10, 30, 20
};

const int eg_king_table[64] = {
    -50,-40,-30,-20,-20,-30,-40,-50,
    -30,-20,-10,  0,  0,-10,-20,-30,
    -30,-10, 20, 30, 30, 20,-10,-30,
    -30,-10, 30, 40, 40, 30,-10,-30,
    -30,-10, 30, 40, 40, 30,-10,-30,
    -30,-10, 20, 30, 30, 20,-10,-30,
    -30,-30,  0,  0,  0,  0,-30,-30,
    -50,-30,-30,-30,-30,-30,-30,-50
};

// Calculate game phase (0-24)
int calculate_phase(const Position& pos) {
    int phase = 0;
    
    // Knights and bishops count as 1 phase each
    phase += count_bits(pos.pieces[WHITE][N]) + count_bits(pos.pieces[BLACK][N]);
    phase += count_bits(pos.pieces[WHITE][B]) + count_bits(pos.pieces[BLACK][B]);
    
    // Rooks count as 2 phases each
    phase += (count_bits(pos.pieces[WHITE][R]) + count_bits(pos.pieces[BLACK][R])) * 2;
    
    // Queens count as 4 phases each
    phase += (count_bits(pos.pieces[WHITE][Q]) + count_bits(pos.pieces[BLACK][Q])) * 4;
    
    // Cap at 24 (full middlegame)
    if (phase > 24) phase = 24;
    
    return phase;
}

// Tapered evaluation function
int evaluate_position_tapered(const Position& pos) {
    int mg_score = 0, eg_score = 0;
    
    for (int color = 0; color < 2; color++) {
        int sign = (color == WHITE) ? 1 : -1;
        
        // Evaluate pawns
        U64 pawns = pos.pieces[color][P];
        while (pawns) {
            int square = lsb_index(pawns);
            pop_bit(pawns, square);
            int eval_square = (color == WHITE) ? square : (63 - square);
            mg_score += sign * (piece_values[P] + pawn_table[eval_square]);
            eg_score += sign * (piece_values[P] + eg_pawn_table[eval_square]);
        }
        
        // Evaluate knights
        U64 knights = pos.pieces[color][N];
        while (knights) {
            int square = lsb_index(knights);
            pop_bit(knights, square);
            int eval_square = (color == WHITE) ? square : (63 - square);
            mg_score += sign * (piece_values[N] + knight_table[eval_square]);
            eg_score += sign * (piece_values[N] + knight_table[eval_square]);
        }
        
        // Evaluate bishops
        U64 bishops = pos.pieces[color][B];
        while (bishops) {
            int square = lsb_index(bishops);
            pop_bit(bishops, square);
            int eval_square = (color == WHITE) ? square : (63 - square);
            mg_score += sign * (piece_values[B] + bishop_table[eval_square]);
            eg_score += sign * (piece_values[B] + bishop_table[eval_square]);
        }
        
        // Evaluate rooks
        U64 rooks = pos.pieces[color][R];
        while (rooks) {
            int square = lsb_index(rooks);
            pop_bit(rooks, square);
            int eval_square = (color == WHITE) ? square : (63 - square);
            mg_score += sign * (piece_values[R] + rook_table[eval_square]);
            eg_score += sign * (piece_values[R] + rook_table[eval_square]);
        }
        
        // Evaluate queens
        U64 queens = pos.pieces[color][Q];
        while (queens) {
            int square = lsb_index(queens);
            pop_bit(queens, square);
            int eval_square = (color == WHITE) ? square : (63 - square);
            mg_score += sign * (piece_values[Q] + queen_table[eval_square]);
            eg_score += sign * (piece_values[Q] + queen_table[eval_square]);
        }
        
        // Evaluate kings with tapered tables
        U64 kings = pos.pieces[color][K];
        while (kings) {
            int square = lsb_index(kings);
            pop_bit(kings, square);
            int eval_square = (color == WHITE) ? square : (63 - square);
            mg_score += sign * (piece_values[K] + mg_king_table[eval_square]);
            eg_score += sign * (piece_values[K] + eg_king_table[eval_square]);
        }
        
        // FIX: Only add mobility to MIDDLEGAME score (not both!)
        mg_score += sign * (eval_mobility(pos, color) / 5);  // Divide by 5 to increase impact
        
        // FIX: Only add king safety to MIDDLEGAME score
        mg_score += sign * eval_king_safety(pos, color);
        
        // ADD: Development evaluation (prevents 2...Nb4 type moves)
        mg_score += sign * eval_development(pos, color);
        
        // Hanging piece detection
        mg_score -= sign * detect_hanging_pieces(pos, color) * 2;  // ✅ DOUBLE the penalty!
        
        // Threat detection
        mg_score -= sign * detect_threats(pos, color) / 10;
        
        // Tactical pattern detection
        mg_score += sign * detect_tactical_patterns(pos, color);
        
        // Trapped piece detection
        mg_score -= sign * detect_trapped_pieces(pos, color);
    }
    
    // Add pawn structure evaluation (FIXED: Only add once per color)
    // Note: eval_pawns() already handles both colors internally, so we only call it once
    int pawn_score = eval_pawns(pos);
    mg_score += pawn_score;
    eg_score += pawn_score;
    
    // Add bishop pair bonus (more valuable in endgame)
    int white_bishops = count_bits(pos.pieces[WHITE][B]);
    int black_bishops = count_bits(pos.pieces[BLACK][B]);
    if (white_bishops >= 2) mg_score += 50;
    if (black_bishops >= 2) mg_score -= 50;
    if (white_bishops >= 2) eg_score += 70;
    if (black_bishops >= 2) eg_score -= 70;
    
    // Calculate phase and interpolate
    int phase = calculate_phase(pos);
    int score = (mg_score * phase + eg_score * (24 - phase)) / 24;
    
    // FIX: Clamp score to prevent overflow (using named constants)
    if (score > EVAL_CLAMP_MAX) score = EVAL_CLAMP_MAX;
    if (score < EVAL_CLAMP_MIN) score = EVAL_CLAMP_MIN;
    
    // FIXED: Return score from side-to-move's perspective
    // Negamax expects the score to be from the current player's perspective
    // But evaluation should ALWAYS return from White's perspective
    // The search handles the negation via -score in negamax
    // Return from side-to-move's perspective for negamax
    return (pos.side_to_move == WHITE) ? score : -score;
}

// ========================================
// INSERT: Pawn Structure Evaluation (Handcrafted ELO)
// ========================================
const int passed_pawn_bonus[8] = { 0, 10, 30, 50, 75, 100, 150, 200 };

// ========================================
// COMPLETE PAWN STRUCTURE EVALUATION
// ========================================

// Add these functions BEFORE eval_pawns():

int eval_doubled_pawns(const Position& pos) {
    int score = 0;
    
    for (int color = 0; color < 2; color++) {
        int sign = (color == WHITE) ? -1 : 1;
        U64 pawns = pos.pieces[color][P];
        
        for (int file = 0; file < 8; file++) {
            int pawn_count = 0;
            for (int rank = 0; rank < 8; rank++) {
                if (get_bit(pawns, rank * 8 + file)) {
                    pawn_count++;
                }
            }
            
            if (pawn_count >= 2) {
                score += sign * 25 * (pawn_count - 1);  // -25 per doubled pawn
            }
        }
    }
    
    return score;
}

int eval_isolated_pawns(const Position& pos) {
    int score = 0;
    
    for (int color = 0; color < 2; color++) {
        int sign = (color == WHITE) ? -1 : 1;
        U64 pawns = pos.pieces[color][P];
        
        for (int file = 0; file < 8; file++) {
            bool has_pawn = false;
            for (int rank = 0; rank < 8; rank++) {
                if (get_bit(pawns, rank * 8 + file)) {
                    has_pawn = true;
                    break;
                }
            }
            
            if (has_pawn) {
                // Check adjacent files
                bool has_neighbor = false;
                if (file > 0) {
                    for (int rank = 0; rank < 8; rank++) {
                        if (get_bit(pawns, rank * 8 + (file - 1))) {
                            has_neighbor = true;
                            break;
                        }
                    }
                }
                if (file < 7 && !has_neighbor) {
                    for (int rank = 0; rank < 8; rank++) {
                        if (get_bit(pawns, rank * 8 + (file + 1))) {
                            has_neighbor = true;
                            break;
                        }
                    }
                }
                
                if (!has_neighbor) {
                    score += sign * 20;  // -20 per isolated pawn
                }
            }
        }
    }
    
    return score;
}

int eval_pawns(const Position& pos) {
    int score = 0;
    U64 wp = pos.pieces[WHITE][P], bp = pos.pieces[BLACK][P];
    
    // Existing passed pawn code...
    // Check White Passed Pawns
    U64 temp_wp = wp;
    while(temp_wp) {
        int sq = lsb_index(temp_wp);
        pop_bit(temp_wp, sq);
        
        // Check if pawn is passed (no enemy pawns in front on same or adjacent files)
        bool is_passed = true;
        int file = sq % 8;
        int rank = sq / 8;
        
        // Check files: current file and adjacent files
        for (int f = std::max(0, file - 1); f <= std::min(7, file + 1); f++) {
            // Check ranks ahead of the pawn (for white, ahead means lower rank numbers in a8=0)
            for (int r = rank - 1; r >= 0; r--) {
                int check_sq = r * 8 + f;
                if (get_bit(bp, check_sq)) {
                    is_passed = false;
                    break;
                }
            }
            if (!is_passed) break;
        }
        
        if (is_passed) {
            int rank_bonus = 7 - rank; // 0 to 7
            score += passed_pawn_bonus[rank_bonus];
        }
    }
    
    // Check Black Passed Pawns
    U64 temp_bp = bp;
    while(temp_bp) {
        int sq = lsb_index(temp_bp);
        pop_bit(temp_bp, sq);
        
        // Check if pawn is passed (no enemy pawns in front on same or adjacent files)
        bool is_passed = true;
        int file = sq % 8;
        int rank = sq / 8;
        
        // Check files: current file and adjacent files
        for (int f = std::max(0, file - 1); f <= std::min(7, file + 1); f++) {
            // Check ranks ahead of the pawn (for black, ahead means higher rank numbers in a8=0)
            for (int r = rank + 1; r < 8; r++) {
                int check_sq = r * 8 + f;
                if (get_bit(wp, check_sq)) {
                    is_passed = false;
                    break;
                }
            }
            if (!is_passed) break;
        }
        
        if (is_passed) {
            int rank_bonus = rank; // 0 to 7
            score -= passed_pawn_bonus[rank_bonus];
        }
    }
    
    // ADD THESE LINES:
    score += eval_doubled_pawns(pos);
    score += eval_isolated_pawns(pos);
    
    return score;  // Don't flip sign here!
}

// ========================================
// Hanging Piece Detection
// ========================================
int detect_hanging_pieces(const Position& pos, int color) {
    int penalty = 0;
    int enemy = 1 - color;
    
    // Check each piece
    for (int piece = P; piece <= Q; piece++) {  // Don't check king
        U64 pieces = pos.pieces[color][piece];
        while (pieces) {
            int sq = lsb_index(pieces);
            pop_bit(pieces, sq);
            
            // Is this piece attacked by enemy?
            if (is_square_attacked(pos, sq, enemy)) {
                // Count defenders
                int defenders = 0;
                
                // Check if defended by pawns
                if (color == WHITE) {
                    if (sq % 8 != 0 && sq + 9 < 64 && get_bit(pos.pieces[WHITE][P], sq + 9)) defenders++;
                    if (sq % 8 != 7 && sq + 7 < 64 && get_bit(pos.pieces[WHITE][P], sq + 7)) defenders++;
                } else {
                    if (sq % 8 != 0 && sq - 9 >= 0 && get_bit(pos.pieces[BLACK][P], sq - 9)) defenders++;
                    if (sq % 8 != 7 && sq - 7 >= 0 && get_bit(pos.pieces[BLACK][P], sq - 7)) defenders++;
                }
                
                // Check if defended by knights
                U64 knight_defenders = knight_attacks[sq] & pos.pieces[color][N];
                defenders += count_bits(knight_defenders);
                
                // Check if defended by bishops/queens
                U64 bishop_defenders = get_bishop_attacks(sq, pos.occupancies[2]) &
                                      (pos.pieces[color][B] | pos.pieces[color][Q]);
                defenders += count_bits(bishop_defenders);
                
                // Check if defended by rooks/queens
                U64 rook_defenders = get_rook_attacks(sq, pos.occupancies[2]) &
                                    (pos.pieces[color][R] | pos.pieces[color][Q]);
                defenders += count_bits(rook_defenders);
                
                // Check if defended by king
                if (king_attacks[sq] & pos.pieces[color][K]) defenders++;
                
                // If undefended or under-defended, apply penalty
                if (defenders == 0) {
                    // Hanging piece - HUGE penalty!
                    penalty += piece_values[piece];
                } else if (defenders == 1) {
                    // Weakly defended - smaller penalty
                    penalty += piece_values[piece] / 4;
                }
            }
        }
    }
    
    return penalty;
}

// ========================================
// Threat Detection
// ========================================
int detect_threats(const Position& pos, int color) {
    int threat_score = 0;
    int enemy = 1 - color;
    
    // Make null move to see what opponent threatens
    Position temp_pos = pos;
    temp_pos.side_to_move = enemy;
    
    // Check what enemy can capture
    for (int piece = P; piece <= Q; piece++) {
        U64 our_pieces = temp_pos.pieces[color][piece];
        while (our_pieces) {
            int sq = lsb_index(our_pieces);
            pop_bit(our_pieces, sq);
            
            if (is_square_attacked(temp_pos, sq, enemy)) {
                // Enemy threatens this piece
                threat_score += piece_values[piece] / 2;
            }
        }
    }
    
    return threat_score;
}

// ========================================
// Tactical Pattern Detection
// ========================================
int detect_tactical_patterns(const Position& pos, int color) {
    int bonus = 0;
    int enemy = 1 - color;
    
    // Check for forks (knight/pawn attacking 2+ valuable pieces)
    U64 knights = pos.pieces[color][N];
    while (knights) {
        int sq = lsb_index(knights);
        pop_bit(knights, sq);
        
        U64 attacks = knight_attacks[sq] & pos.occupancies[enemy];
        int valuable_targets = 0;
        int target_value = 0;
        
        while (attacks) {
            int target = lsb_index(attacks);
            pop_bit(attacks, target);
            
            for (int p = N; p <= Q; p++) {  // Only count valuable pieces
                if (get_bit(pos.pieces[enemy][p], target)) {
                    valuable_targets++;
                    target_value += piece_values[p];
                    break;
                }
            }
        }
        
        if (valuable_targets >= 2) {
            bonus += target_value / 4;  // Fork bonus
        }
    }
    
    // Check for pins (bishop/rook/queen pinning enemy piece to king/queen)
    U64 bishops = pos.pieces[color][B] | pos.pieces[color][Q];
    U64 enemy_king = pos.pieces[enemy][K];
    int enemy_king_sq = lsb_index(enemy_king);
    
    while (bishops) {
        int sq = lsb_index(bishops);
        pop_bit(bishops, sq);
        
        U64 attacks = get_bishop_attacks(sq, pos.occupancies[2]);
        
        // Check if bishop attacks enemy king
        if (get_bit(attacks, enemy_king_sq)) {
            // Check for pieces between bishop and king
            U64 between = get_bishop_attacks(sq, pos.occupancies[2] ^ (1ULL << enemy_king_sq));
            U64 pinned = between & pos.occupancies[enemy] & ~enemy_king;
            
            if (count_bits(pinned) == 1) {
                // Found a pin!
                int pinned_sq = lsb_index(pinned);
                for (int p = P; p <= Q; p++) {
                    if (get_bit(pos.pieces[enemy][p], pinned_sq)) {
                        bonus += piece_values[p] / 3;  // Pin bonus
                        break;
                    }
                }
            }
        }
    }
    
    // Similar for rook pins...
    U64 rooks = pos.pieces[color][R] | pos.pieces[color][Q];
    while (rooks) {
        int sq = lsb_index(rooks);
        pop_bit(rooks, sq);
        
        U64 attacks = get_rook_attacks(sq, pos.occupancies[2]);
        
        if (get_bit(attacks, enemy_king_sq)) {
            U64 between = get_rook_attacks(sq, pos.occupancies[2] ^ (1ULL << enemy_king_sq));
            U64 pinned = between & pos.occupancies[enemy] & ~enemy_king;
            
            if (count_bits(pinned) == 1) {
                int pinned_sq = lsb_index(pinned);
                for (int p = P; p <= Q; p++) {
                    if (get_bit(pos.pieces[enemy][p], pinned_sq)) {
                        bonus += piece_values[p] / 3;
                        break;
                    }
                }
            }
        }
    }
    
    return bonus;
}

// ========================================
// Trapped Piece Detection
// ========================================
int detect_trapped_pieces(const Position& pos, int color) {
    int penalty = 0;
    
    // Check bishops
    U64 bishops = pos.pieces[color][B];
    while (bishops) {
        int sq = lsb_index(bishops);
        pop_bit(bishops, sq);
        
        // Count escape squares
        U64 moves = get_bishop_attacks(sq, pos.occupancies[2]) & ~pos.occupancies[color];
        int escape_squares = count_bits(moves);
        
        if (escape_squares <= 2) {
            penalty += 50;  // Bishop is trapped or nearly trapped
        }
    }
    
    // Check knights
    U64 knights = pos.pieces[color][N];
    while (knights) {
        int sq = lsb_index(knights);
        pop_bit(knights, sq);
        
        U64 moves = knight_attacks[sq] & ~pos.occupancies[color];
        int escape_squares = count_bits(moves);
        
        if (escape_squares <= 1) {
            penalty += 100;  // Knight is trapped
        } else if (escape_squares <= 3) {
            penalty += 30;   // Knight has limited mobility
        }
    }
    
    // Check rooks
    U64 rooks = pos.pieces[color][R];
    while (rooks) {
        int sq = lsb_index(rooks);
        pop_bit(rooks, sq);
        
        U64 moves = get_rook_attacks(sq, pos.occupancies[2]) & ~pos.occupancies[color];
        int escape_squares = count_bits(moves);
        
        if (escape_squares <= 3) {
            penalty += 40;  // Rook is trapped
        }
    }
    
    return penalty;
}


// ========================================
// DEVELOPMENT EVALUATION (CRITICAL FIX!)
// ========================================
int eval_development(const Position& pos, int color) {
    int score = 0;
    int phase = calculate_phase(pos);
    
    // Only apply in opening/early middlegame
    if (phase < 18) {
        // Penalty for pieces on starting squares
        if (color == WHITE) {
            // Knights
            if (get_bit(pos.pieces[WHITE][N], b1)) score -= 10;
            if (get_bit(pos.pieces[WHITE][N], g1)) score -= 10;
            
            // Bishops
            if (get_bit(pos.pieces[WHITE][B], c1)) score -= 10;
            if (get_bit(pos.pieces[WHITE][B], f1)) score -= 10;
            
            // Rooks
            if (get_bit(pos.pieces[WHITE][R], a1)) score -= 5;
            if (get_bit(pos.pieces[WHITE][R], h1)) score -= 5;
            
            // Queen
            if (get_bit(pos.pieces[WHITE][Q], d1)) score -= 5;
        } else {
            // Similar for black
            if (get_bit(pos.pieces[BLACK][N], b8)) score -= 10;
            if (get_bit(pos.pieces[BLACK][N], g8)) score -= 10;
            if (get_bit(pos.pieces[BLACK][B], c8)) score -= 10;
            if (get_bit(pos.pieces[BLACK][B], f8)) score -= 10;
            if (get_bit(pos.pieces[BLACK][R], a8)) score -= 5;
            if (get_bit(pos.pieces[BLACK][R], h8)) score -= 5;
            if (get_bit(pos.pieces[BLACK][Q], d8)) score -= 5;
        }
        
        // Bonus for castling
        if (color == WHITE) {
            if (!(pos.castling_rights & 3)) {  // Already castled
                score += 30;
            }
        } else {
            if (!(pos.castling_rights & 12)) {
                score += 30;
            }
        }
    }
    
    return score;
}

// ========================================
// FIXED KING SAFETY EVALUATION
// ========================================
int eval_king_safety(const Position& pos, int color) {
    U64 king_bb = pos.pieces[color][K];
    if (king_bb == 0) return 0;
    
    int king_sq = lsb_index(king_bb);
    int score = 0;
    int enemy = 1 - color;
    
    // 1. PENALTY FOR KING NOT ON BACK RANK (CRITICAL!)
    int king_rank = king_sq / 8;
    if (color == WHITE) {
        if (king_rank < 7) {  // Not on rank 1
            score -= (7 - king_rank) * 50;  // -50 per rank away from back rank
        }
    } else {
        if (king_rank > 0) {  // Not on rank 8
            score -= king_rank * 50;  // -50 per rank away from back rank
        }
    }
    
    // 2. PENALTY FOR EXPOSED KING (no pawn shield)
    int king_file = king_sq % 8;
    int pawn_shield_count = 0;
    
    // Check for pawns in front of king
    if (color == WHITE) {
        // Check squares in front (rank - 1)
        if (king_rank > 0) {
            for (int f = std::max(0, king_file - 1); f <= std::min(7, king_file + 1); f++) {
                int check_sq = (king_rank - 1) * 8 + f;
                if (get_bit(pos.pieces[WHITE][P], check_sq)) {
                    pawn_shield_count++;
                }
            }
        }
    } else {
        // Check squares in front (rank + 1)
        if (king_rank < 7) {
            for (int f = std::max(0, king_file - 1); f <= std::min(7, king_file + 1); f++) {
                int check_sq = (king_rank + 1) * 8 + f;
                if (get_bit(pos.pieces[BLACK][P], check_sq)) {
                    pawn_shield_count++;
                }
            }
        }
    }
    
    score += pawn_shield_count * 20;  // +20 per pawn in shield
    
    // 3. PENALTY FOR KING UNDER ATTACK
    int attackers = 0;
    
    // Count enemy pieces attacking king zone (king + adjacent squares)
    for (int dr = -1; dr <= 1; dr++) {
        for (int df = -1; df <= 1; df++) {
            int check_rank = king_rank + dr;
            int check_file = king_file + df;
            
            if (check_rank >= 0 && check_rank < 8 && check_file >= 0 && check_file < 8) {
                int check_sq = check_rank * 8 + check_file;
                if (is_square_attacked(pos, check_sq, enemy)) {
                    attackers++;
                }
            }
        }
    }
    
    score -= attackers * 15;  // -15 per attacking square
    
    // 4. PENALTY FOR OPEN FILES NEAR KING
    for (int f = std::max(0, king_file - 1); f <= std::min(7, king_file + 1); f++) {
        bool has_pawn = false;
        for (int r = 0; r < 8; r++) {
            if (get_bit(pos.pieces[color][P], r * 8 + f)) {
                has_pawn = true;
                break;
            }
        }
        if (!has_pawn) {
            score -= 30;  // -30 per open file near king
        }
    }
    
    // 5. BONUS FOR CASTLING RIGHTS (if still available)
    if (color == WHITE) {
        if (pos.castling_rights & 1) score += 15;  // Kingside
        if (pos.castling_rights & 2) score += 15;  // Queenside
    } else {
        if (pos.castling_rights & 4) score += 15;  // Kingside
        if (pos.castling_rights & 8) score += 15;  // Queenside
    }
    
    // 6. MASSIVE PENALTY FOR KING IN CENTER (middlegame)
    int phase = calculate_phase(pos);
    if (phase > 12) {  // Middlegame
        int center_dist = std::min({king_file, 7 - king_file, king_rank, 7 - king_rank});
        if (center_dist >= 2) {  // King in center 4x4
            score -= 100;  // HUGE penalty for exposed king
        }
    }
    
    return score;
}

// ========================================
// INSERT: Mobility Evaluation
// ========================================
int eval_mobility(const Position& pos, int color) {
    int score = 0;
    U64 occ = pos.occupancies[2];
    
    // Knight mobility
    U64 knights = pos.pieces[color][N];
    while (knights) {
        int sq = lsb_index(knights);
        knights ^= (1ULL << sq); // Use XOR instead of pop_bit to avoid modifying original
        if (sq < 0 || sq >= 64) continue; // Bounds check
        score += count_bits(knight_attacks[sq] & ~pos.occupancies[color]) * 4;
    }
    
    // Bishop mobility
    U64 bishops = pos.pieces[color][B];
    while (bishops) {
        int sq = lsb_index(bishops);
        bishops ^= (1ULL << sq); // Use XOR instead of pop_bit to avoid modifying original
        score += count_bits(get_bishop_attacks(sq, occ) & ~pos.occupancies[color]) * 3;
    }
    
    // Rook mobility
    U64 rooks = pos.pieces[color][R];
    while (rooks) {
        int sq = lsb_index(rooks);
        rooks ^= (1ULL << sq); // Use XOR instead of pop_bit to avoid modifying original
        score += count_bits(get_rook_attacks(sq, occ) & ~pos.occupancies[color]) * 2;
    }
    
    return score;
}

// ========================================
// INSERT: Transposition Table & Heuristics
// ========================================

void clear_tt() {
    for (int i = 0; i < TT_SIZE; i++) TTable[i] = TTEntry();
}

// Clear history heuristic and killer moves
void clear_history() {
    memset(killer_moves, 0, sizeof(killer_moves));
    memset(history_moves, 0, sizeof(history_moves));
}

// Write to TT with ply parameter for mate score adjustment (FIXED)
void record_tt(U64 hash, int score, int flag, int depth, Move move, int ply) {
    int index = hash % TT_SIZE;
    
    // FIX: Only store if this entry is deeper or doesn't exist
    if (TTable[index].key == hash && TTable[index].depth > depth) {
        return; // Don't overwrite deeper entries
    }
    
    // FIXED: Correct mate score adjustment
    int stored_score = score;
    if (score > MATE_SCORE - MAX_PLY) {
        // Mate-in-N: ADD ply when storing (make it relative to root)
        stored_score = score + ply;  // FIX: ADD ply when storing
    } else if (score < -MATE_SCORE + MAX_PLY) {
        // Mated-in-N: SUBTRACT ply when storing
        stored_score = score - ply;  // FIX: SUBTRACT ply when storing
    }
    
    TTable[index].key = hash;
    TTable[index].score = stored_score;
    TTable[index].flag = flag;
    TTable[index].depth = depth;
    TTable[index].move = move;
}

// Read from TT with ply parameter for mate score adjustment (FIXED)
bool probe_tt(U64 hash, int depth, int alpha, int beta, int& score, Move& best_move, int ply) {
    int index = hash % TT_SIZE;
    TTEntry& entry = TTable[index];
    
    if (entry.key != hash) return false;
    
    best_move = entry.move;
    
    // FIX: Only use TT entry if it's from SAME OR DEEPER search
    if (entry.depth < depth) return false;
    
    // FIXED: Correct mate score adjustment
    int adjusted_score = entry.score;
    
    if (adjusted_score > MATE_SCORE - MAX_PLY) {
        // Mate-in-N: SUBTRACT ply when retrieving (make it relative to current position)
        adjusted_score = adjusted_score - ply;  // FIX: SUBTRACT ply when retrieving
    } else if (adjusted_score < -MATE_SCORE + MAX_PLY) {
        // Mated-in-N: ADD ply when retrieving
        adjusted_score = adjusted_score + ply;  // FIX: ADD ply when retrieving
    }
    
    // FIX: Sanity check - if adjusted score is in mate range but original wasn't, reject it
    if (adjusted_score > MATE_SCORE - MAX_PLY || adjusted_score < -MATE_SCORE + MAX_PLY) {
        if (entry.score < MATE_SCORE - MAX_PLY && entry.score > -MATE_SCORE + MAX_PLY) {
            return false; // Corrupted entry - reject it
        }
    }
    
    // ADD: Reject mate scores at shallow depths - they're likely corrupted
    if ((adjusted_score > MATE_SCORE - 20 || adjusted_score < -MATE_SCORE + 20) && depth < 15) {
        return false;
    }
    
    if (entry.flag == TT_EXACT) {
        score = adjusted_score;
        return true;
    }
    if (entry.flag == TT_ALPHA && adjusted_score <= alpha) {
        score = alpha;
        return true;
    }
    if (entry.flag == TT_BETA && adjusted_score >= beta) {
        score = beta;
        return true;
    }
    
    return false;
}


// ========================================
// Square Attacked (Needed for Pruning)
// ========================================
bool is_square_attacked(const Position& pos, int square, int side) {
    // Safety check for invalid squares
    if (square < 0 || square >= 64) return false;
    
    // Check Knight attacks
    if (knight_attacks[square] & pos.pieces[side][N]) return true;
    
    // Check King attacks
    if (king_attacks[square] & pos.pieces[side][K]) return true;
    
    // Check Sliding Pieces (B/R/Q)
    U64 occ = pos.occupancies[2];
    if (get_bishop_attacks(square, occ) & (pos.pieces[side][B] | pos.pieces[side][Q])) return true;
    if (get_rook_attacks(square, occ) & (pos.pieces[side][R] | pos.pieces[side][Q])) return true;
    
    // Pawn attacks (a8=0 coordinate system)
    if (side == WHITE) {
        // White pawns attack UP-LEFT (-9) and UP-RIGHT (-7)
        // To check if square X is attacked by white pawns:
        // Check for pawn at X + 9 (attacks X with -9, i.e., from down-right)
        // Check for pawn at X + 7 (attacks X with -7, i.e., from down-left)
        
        // square + 9: pawn on right-down attacks square with -9 (left-up)
        // square + 9 moves right (+1 file), so square must not be on h-file (file 7)
        if (square % 8 != 7 && square + 9 < 64 && get_bit(pos.pieces[WHITE][P], square + 9)) return true;
        
        // square + 7: pawn on left-down attacks square with -7 (right-up)
        // square + 7 moves left (-1 file), so square must not be on a-file (file 0)
        if (square % 8 != 0 && square + 7 < 64 && get_bit(pos.pieces[WHITE][P], square + 7)) return true;
    } else {
        // Black pawns attack DOWN-LEFT (+7) and DOWN-RIGHT (+9)
        // To check if square X is attacked by black pawns:
        // Check for pawn at X - 9 (attacks X with +9, i.e., from up-left)
        // Check for pawn at X - 7 (attacks X with +7, i.e., from up-right)
        
        // square - 9: pawn on left-up attacks square with +9 (right-down)
        // square - 9 moves left (-1 file), so square must not be on a-file (file 0)
        if (square % 8 != 0 && square - 9 >= 0 && get_bit(pos.pieces[BLACK][P], square - 9)) return true;
        
        // square - 7: pawn on right-up attacks square with +7 (left-down)
        // square - 7 moves right (+1 file), so square must not be on h-file (file 7)
        if (square % 8 != 7 && square - 7 >= 0 && get_bit(pos.pieces[BLACK][P], square - 7)) return true;
    }
    
    return false;
}

// Check for 3-fold repetition (FIXED: Correct counting)
bool is_repetition(const Position& pos) {
    int repetitions = 0;
    U64 current_hash = pos.hash_key;
    
    // Only check positions since last irreversible move (pawn move/capture)
    // This is limited by halfmove_clock
    int start_idx = std::max(0, (int)position_history.size() - halfmove_clock);
    
    for (int i = start_idx; i < (int)position_history.size(); i++) {
        if (position_history[i] == current_hash) {
            repetitions++;
            if (repetitions >= 2) return true;
        }
    }
    
    return false;
}

// Check for 50-move rule
bool is_fifty_move_rule() {
    return halfmove_clock >= 100; // 50 moves = 100 half-moves
}

// ========================================
// Move Ordering (Basic)
// ========================================

// ========================================
// Enhanced Move Scoring (Uses TT Move!)
// ========================================

// Static Exchange Evaluation (SEE) - Critical for capture ordering
// Full Static Exchange Evaluation (SEE) - simulates entire exchange sequence
int see_capture(const Position& pos, const Move& move) {
    int from = move.get_from();
    int to = move.get_to();
    int attacker = move.get_piece();
    
    // Find victim
    int victim = -1;
    int enemy = 1 - pos.side_to_move;
    for (int p = 0; p < 6; p++) {
        if (get_bit(pos.pieces[enemy][p], to)) {
            victim = p;
            break;
        }
    }
    
    if (victim == -1) return 0; // Not a capture
    
    // Start exchange sequence: attacker takes victim
    int gain = piece_values[victim] - piece_values[attacker];
    
    // If we're already ahead and opponent has no attackers, this is good
    if (gain >= 0) {
        // Check if square is still defended after our capture
        Position temp_pos = pos;
        // Simulate the capture
        temp_pos.pieces[pos.side_to_move][attacker] ^= (1ULL << from);
        temp_pos.pieces[enemy][victim] ^= (1ULL << to);
        temp_pos.pieces[pos.side_to_move][attacker] ^= (1ULL << to);
        temp_pos.occupancies[pos.side_to_move] ^= (1ULL << from) ^ (1ULL << to);
        temp_pos.occupancies[enemy] ^= (1ULL << to);
        temp_pos.occupancies[2] ^= (1ULL << from);
        
        // If opponent can't recapture, this is a good trade
        if (!is_square_attacked(temp_pos, to, enemy)) {
            return gain;
        }
    }
    
    // Full exchange simulation
    // We need to find the sequence of attackers from both sides
    std::vector<int> attackers; // Piece values in order of attack
    
    // Add our initial attacker
    attackers.push_back(piece_values[attacker]);
    
    // Find all attackers for both sides, sorted by piece value (MVV/LVA order)
    Position temp_pos = pos;
    // Remove the capturing piece temporarily to find next attackers
    temp_pos.pieces[pos.side_to_move][attacker] ^= (1ULL << from);
    temp_pos.occupancies[pos.side_to_move] ^= (1ULL << from);
    temp_pos.occupancies[2] ^= (1ULL << from);
    
    bool our_turn = false; // Next attacker will be opponent
    
    // Simulate exchange sequence up to 16 moves (8 per side should be enough)
    for (int depth = 0; depth < 16; depth++) {
        int next_attacker = -1;
        int next_attacker_value = 10000; // High value to find minimum
        
        // Find the least valuable attacker of the current side
        int current_side = our_turn ? enemy : pos.side_to_move;
        U64 occ = temp_pos.occupancies[2];
        
        // Check all piece types from P to K (cheapest to most expensive)
        for (int piece_type = P; piece_type <= K; piece_type++) {
            U64 pieces = temp_pos.pieces[current_side][piece_type];
            while (pieces) {
                int sq = lsb_index(pieces);
                pop_bit(pieces, sq);
                
                // Check if this piece attacks the target square
                bool attacks = false;
                switch (piece_type) {
                    case P: {
                        // Pawn attacks depend on color
                        if (current_side == WHITE) {
                            if (sq % 8 != 0 && sq - 9 == to) attacks = true; // Left capture (up-left)
                            if (sq % 8 != 7 && sq - 7 == to) attacks = true; // Right capture (up-right)
                        } else {
                            if (sq % 8 != 0 && sq + 7 == to) attacks = true; // Left capture (down-left)
                            if (sq % 8 != 7 && sq + 9 == to) attacks = true; // Right capture (down-right)
                        }
                        break;
                    }
                    case N: {
                        attacks = (knight_attacks[sq] & (1ULL << to)) != 0;
                        break;
                    }
                    case B: {
                        attacks = (get_bishop_attacks(sq, occ) & (1ULL << to)) != 0;
                        break;
                    }
                    case R: {
                        attacks = (get_rook_attacks(sq, occ) & (1ULL << to)) != 0;
                        break;
                    }
                    case Q: {
                        attacks = (get_queen_attacks(sq, occ) & (1ULL << to)) != 0;
                        break;
                    }
                    case K: {
                        attacks = (king_attacks[sq] & (1ULL << to)) != 0;
                        break;
                    }
                }
                
                if (attacks && piece_values[piece_type] < next_attacker_value) {
                    next_attacker_value = piece_values[piece_type];
                    next_attacker = piece_type;
                    break; // Found the least valuable attacker for this side
                }
            }
            if (next_attacker != -1) break; // Found an attacker
        }
        
        if (next_attacker == -1) {
            // No more attackers - exchange is over
            break;
        }
        
        // Add this attacker to the sequence
        attackers.push_back(piece_values[next_attacker]);
        
        // Remove this attacker from the board (it will be captured)
        U64 piece_bb = temp_pos.pieces[current_side][next_attacker];
        int attacker_sq = -1;
        U64 temp = piece_bb;
        while (temp) {
            int sq = lsb_index(temp);
            pop_bit(temp, sq);
            if ((get_bishop_attacks(sq, occ) & (1ULL << to)) != 0 ||
                (get_rook_attacks(sq, occ) & (1ULL << to)) != 0 ||
                (get_queen_attacks(sq, occ) & (1ULL << to)) != 0 ||
                (knight_attacks[sq] & (1ULL << to)) != 0 ||
                (king_attacks[sq] & (1ULL << to)) != 0 ||
                // Pawn attacks (a8=0 coordinate system)
                (current_side == WHITE && ((sq % 8 != 7 && sq - 9 == to) || (sq % 8 != 0 && sq - 7 == to))) ||
                (current_side == BLACK && ((sq % 8 != 0 && sq + 7 == to) || (sq % 8 != 7 && sq + 9 == to)))) {
                attacker_sq = sq;
                break;
            }
        }
        
        if (attacker_sq != -1) {
            temp_pos.pieces[current_side][next_attacker] ^= (1ULL << attacker_sq);
            temp_pos.occupancies[current_side] ^= (1ULL << attacker_sq);
            temp_pos.occupancies[2] ^= (1ULL << attacker_sq);
        }
        
        our_turn = !our_turn; // Switch sides
    }
    
    // Calculate the exchange value using the sequence
    // Start with victim value, then alternate subtracting/adding attacker values
    int exchange_value = piece_values[victim];
    bool our_gain = false; // First capture (victim) benefits us
    
    for (size_t i = 0; i < attackers.size(); i++) {
        if (our_gain) {
            exchange_value += attackers[i];
        } else {
            exchange_value -= attackers[i];
        }
        our_gain = !our_gain;
    }
    
    return exchange_value;
}

int score_move_enhanced(const Position& pos, const Move& move, const Move& tt_move, int ply = 0) {
    if (move.move == tt_move.move) return 100000; // TT move highest priority
    
    // Winning captures (SEE-based) - Critical improvement
    if (move.is_capture()) {
        int see_score = see_capture(pos, move);
        if (see_score >= 0) {
            return 50000 + see_score; // Good captures
        } else {
            return 5000 + see_score; // Bad captures (search last)
        }
    }
    
    // Promotions
    if (move.get_promo() == Q) return 40000;
    if (move.get_promo() != 0) return 30000;
    
    // Killer moves
    if (ply < MAX_DEPTH) {
        if (killer_moves[0][ply].move == move.move) return 20000;
        if (killer_moves[1][ply].move == move.move) return 19000;
    }
    
    // History heuristic
    int piece = move.get_piece();
    int to = move.get_to();
    return history_moves[piece][to];
}

void sort_moves_enhanced(const Position& pos, std::vector<Move>& moves, const Move& tt_move, int ply) {
    std::sort(moves.begin(), moves.end(), [&](const Move& a, const Move& b) {
        return score_move_enhanced(pos, a, tt_move, ply) > score_move_enhanced(pos, b, tt_move, ply);
    });
}

// ========================================
// INSERT: SEE (Static Exchange Evaluation)
// ========================================

// ========================================
// REPLACE: Quiescence Search (FIXED with Ply)
// ========================================

int quiescence(Position& pos, int alpha, int beta, int ply) {
    // FIXED: Check time every 128 nodes (more aggressive time management)
    if ((nodes_searched & 127) == 0) {
        // Add 5% safety margin to prevent overshooting time limit
        if (current_time_ms() - start_time > (time_limit * 95 / 100)) {
            time_up = true;
            return 0;  // RETURN IMMEDIATELY!
        }
    }
    if (time_up) return 0;
    if (ply >= MAX_DEPTH - 1) return evaluate_position_tapered(pos);

    nodes_searched++;

    // Stand pat
    int stand_pat = evaluate_position_tapered(pos);
    
    // Delta pruning: if position + queen value < alpha, skip captures (using named constant)
    if (stand_pat + piece_values[Q] + DELTA_PRUNING_MARGIN < alpha) {
        return alpha;
    }
    
    if (stand_pat >= beta) return beta;
    if (stand_pat > alpha) alpha = stand_pat;

    // Generate captures directly for better performance
    std::vector<Move> captures;
    generate_captures(pos, captures);
    
    // Delta pruning for captures: if stand-pat + victim value + 200 < alpha, skip
    std::vector<Move> filtered_captures;
    for (const auto& move : captures) {
        if (move.is_capture()) {
            int victim = P;
            int enemy = 1 - pos.side_to_move;
            int to = move.get_to();
            for (int p = 0; p < 6; p++) {
                if (get_bit(pos.pieces[enemy][p], to)) {
                    victim = p;
                    break;
                }
            }
            if (stand_pat + piece_values[victim] + 200 >= alpha) {
                filtered_captures.push_back(move);
            }
        } else {
            filtered_captures.push_back(move);  // Keep non-captures
        }
    }
    captures = filtered_captures;
    
    sort_moves_enhanced(pos, captures, Move(), 0);

    for (const auto& move : captures) {
        BoardState state = make_move(pos, move);
        
        // ✅ ADD: Check if move is legal (king not in check)
        int our_color = 1 - pos.side_to_move;  // We just switched sides
        U64 king_bb = pos.pieces[our_color][K];
        if (king_bb != 0) {
            int king_sq = lsb_index(king_bb);
            if (is_square_attacked(pos, king_sq, pos.side_to_move)) {
                unmake_move(pos, move, state);
                continue;  // Skip illegal move
            }
        }
        
        int score = -quiescence(pos, -beta, -alpha, ply + 1);
        
        unmake_move(pos, move, state);
        
        if (time_up) return 0;

        if (score >= beta) return beta;
        if (score > alpha) alpha = score;
    }
    
    return alpha;
}

// ========================================
// REPLACE: Negamax (FIXED with Legal Moves and PV Table)
// ========================================

// Principal Variation Search (PVS) - More efficient than regular negamax
int pvs_search(Position& pos, int depth, int alpha, int beta, int ply, bool is_pv_node) {
    // FIXED: Check time every 128 nodes (more aggressive time management)
    if ((nodes_searched & 127) == 0) {
        // Add 10% safety margin to prevent overshooting time limit
        if (current_time_ms() - start_time > (time_limit * 90 / 100)) {
            time_up = true;
            return 0;  // RETURN IMMEDIATELY!
        }
    }
    if (time_up) return 0;
    if (ply >= MAX_DEPTH - 1) return evaluate_position_tapered(pos);
    
    // Initialize PV length for this ply
    pv_length[ply] = ply;
    
    // Initialize search variables
    int flag = TT_ALPHA;
    Move best_move_found;
    
    nodes_searched++;

    // Transposition table probe
    int tt_score = 0;
    Move tt_move;
    if (probe_tt(pos.hash_key, depth, alpha, beta, tt_score, tt_move, ply)) {
        return tt_score;
    }

    // Leaf node - use quiescence
    if (depth <= 0) {
        return quiescence(pos, alpha, beta, ply);
    }

    // Check extension
    bool in_check = false;
    U64 king_bb = pos.pieces[pos.side_to_move][K];
    if (king_bb != 0) {
        int king_sq = lsb_index(king_bb);
        in_check = is_square_attacked(pos, king_sq, 1 - pos.side_to_move);
        if (in_check) depth++;
    }
    
    // Check for repetition draw
    if (is_repetition(pos)) {
        return 0; // Draw score
    }
    
    // Check 50-move rule
    if (is_fifty_move_rule()) {
        return 0; // Draw score
    }

    // Razoring
    // FIX: Add named constants for razoring margins
    const int RAZOR_MARGIN_BASE = 300;
    const int RAZOR_MARGIN_DEPTH = 100;
    
    if (depth <= 3 && !in_check && alpha < MATE_SCORE - 100) {
        int static_eval = evaluate_position_tapered(pos);
        int razor_margin = RAZOR_MARGIN_BASE + RAZOR_MARGIN_DEPTH * depth;
        
        if (static_eval + razor_margin < alpha) {
            int q_score = quiescence(pos, alpha - razor_margin, alpha - razor_margin + 1, ply);
            if (q_score + razor_margin < alpha) {
                return q_score;
            }
        }
    }

    // Null Move Pruning (skip in PV nodes)
    if (!is_pv_node && depth >= 3 && !in_check && ply > 0) {
        // FIX: Don't add null moves to position history to avoid false repetition draws
        // Make null move (just switch sides)
        pos.side_to_move = 1 - pos.side_to_move;
        pos.hash_key ^= side_key;
        // Note: NOT adding to position_history to prevent repetition detection issues
        
        int null_score = -pvs_search(pos, depth - 1 - 2, -beta, -beta + 1, ply + 1, false);
        
        // Unmake null move
        pos.side_to_move = 1 - pos.side_to_move;
        pos.hash_key ^= side_key;
        // Note: No need to remove from history since we didn't add it
        
        if (null_score >= beta) {
            return beta;
        }
    }

    // Generate LEGAL moves only
    MoveList move_list = generate_legal_moves(pos);
    
    // Terminal node detection
    if (move_list.moves.empty()) {
        if (in_check) {
            return -MATE_SCORE + ply; // Checkmate
        }
        return 0; // Stalemate
    }

    // Futility pruning (FIXED: Use named constant)
    bool futility_pruning = false;
    if (depth <= 3 && !in_check && alpha < MATE_SCORE - 100 && beta > -MATE_SCORE + 100) {
        int static_eval = evaluate_position_tapered(pos);
        if (static_eval + FUTILITY_MARGIN * depth < alpha) {
            futility_pruning = true;
        }
    }
    
    // Reverse futility pruning (FIXED: Use different margin)
    if (depth >= 3 && !in_check && !is_pv_node && alpha > -MATE_SCORE + 100) {
        int static_eval = evaluate_position_tapered(pos);
        if (static_eval - REVERSE_FUTILITY_MARGIN * depth > beta) {
            return static_eval - REVERSE_FUTILITY_MARGIN * depth;
        }
    }
    
    // Multi-cut pruning removed - implementation was broken
    // The current implementation tries to search without making moves, which is incorrect
    // Multi-cut requires trying multiple moves, not just a single reduced search

    // Internal Iterative Deepening (IID) - if no TT move and depth >= 4
    if (depth >= 4 && tt_move.move == 0) {
        // Search at reduced depth to find a good move (score is discarded, only used for move ordering)
        int iid_depth = depth - 2;
        (void)-pvs_search(pos, iid_depth, -beta, -alpha, ply + 1, false);
        // Probe TT to get the move from the IID search
        Move iid_move;
        int dummy_score;
        if (probe_tt(pos.hash_key, iid_depth, alpha, beta, dummy_score, iid_move, ply)) {
            // FIX: Always use IID move for sorting, regardless of score
            // The IID search is meant to find a good move for ordering
            tt_move = iid_move;
        }
    }
    
    // Singular extensions removed - was causing false mate scores
    
    // Sort moves
    sort_moves_enhanced(pos, move_list.moves, tt_move, ply);

    bool searched_first_move = false;

    for (size_t i = 0; i < move_list.moves.size(); i++) {
        const Move& move = move_list.moves[i];
        
        // Skip quiet moves if futility pruning is active (FIXED: Remove redundant !in_check)
        if (futility_pruning && !move.is_capture() && !move.get_promo()) {
            continue;
        }
        
        // LMR (Late Move Reductions) - Critical for search efficiency
        int reduction = 0;
        
        // LMR conditions: depth >= 3, not first few moves, not in check, not capture/promotion
        if (depth >= 3 && i >= 4 && !in_check && !move.is_capture() && !move.get_promo()) {
            // Base reduction
            reduction = 1;
            
            // Increase reduction for later moves
            if (i >= 8) reduction = 2;
            if (i >= 16) reduction = 3;
            
            // Reduce less in PV nodes
            if (is_pv_node) reduction = std::max(0, reduction - 1);
            
            // Reduce less for killer moves
            if (killer_moves[0][ply].move == move.move ||
                killer_moves[1][ply].move == move.move) {
                reduction = std::max(0, reduction - 1);
            }
        }
        
        // Make the move
        BoardState state = make_move(pos, move);

        int score;
        
        // PVS: First move gets full window search
        if (!searched_first_move) {
            score = -pvs_search(pos, depth - 1, -beta, -alpha, ply + 1, true);
            searched_first_move = true;
        } else {
            // Other moves: use LMR with null window search
            if (reduction > 0) {
                // Reduced search
                score = -pvs_search(pos, depth - 1 - reduction, -alpha - 1, -alpha, ply + 1, false);
                
                // Re-search at full depth if reduced search beats alpha
                if (score > alpha) {
                    score = -pvs_search(pos, depth - 1, -alpha - 1, -alpha, ply + 1, false);
                }
            } else {
                // No reduction - normal null window search
                score = -pvs_search(pos, depth - 1, -alpha - 1, -alpha, ply + 1, false);
            }
            
            // If score > alpha, do full window re-search
            if (score > alpha && score < beta) {
                score = -pvs_search(pos, depth - 1, -beta, -alpha, ply + 1, true);
            }
        }
        
        // Unmake the move (ONLY ONCE!)
        unmake_move(pos, move, state);

        if (time_up) return 0;

        if (score > alpha) {
            alpha = score;
            flag = TT_EXACT;
            best_move_found = move;
            
            // Update Killer Moves
            if (ply < MAX_DEPTH) {
                if (killer_moves[0][ply].move != move.move) {
                    killer_moves[1][ply] = killer_moves[0][ply];
                    killer_moves[0][ply] = move;
                }
            }
            
            // Update History Heuristic
            if (ply < MAX_DEPTH && !move.is_capture()) {
                int piece = move.get_piece();
                int to = move.get_to();
                if (history_moves[piece][to] < HISTORY_MAX) { // Prevent overflow (using named constant)
                    history_moves[piece][to] += depth * depth;
                }
            }
            
            // Update PV table with overflow protection (FIXED: Correct bounds)
            pv_table[ply][0] = move;
            pv_length[ply] = 1;
            for (int i = 0; i < pv_length[ply + 1] && i < MAX_PLY - ply - 1; i++) {
                pv_table[ply][pv_length[ply]] = pv_table[ply + 1][i];
                pv_length[ply]++;
            }
            
            if (alpha >= beta) {
                // Countermove table removed - current implementation is broken
                // Proper countermove tracking requires move history which isn't implemented
                
                record_tt(pos.hash_key, beta, TT_BETA, depth, move, ply);
                return beta;
            }
        }
    }

    record_tt(pos.hash_key, alpha, flag, depth, best_move_found, ply);
    return alpha;
}


// ========================================
// INSERT: Safe Move Printing Function
// ========================================

void print_move_uci(int move_int) {
    if (move_int == 0) {
        std::cout << "0000";
        return;
    }
    
    int from = move_int & 0x3F;
    int to = (move_int >> 6) & 0x3F;
    int promoted = (move_int >> 15) & 0x7;
    
    std::cout << square_to_algebraic(from) << square_to_algebraic(to);
    
    if (promoted) {
        // FIX: Add bounds check to prevent out-of-bounds access
        if (promoted > 0 && promoted < 5) {
            const char promos[] = {' ', 'n', 'b', 'r', 'q'};
            std::cout << promos[promoted];
        }
    }
}

// ========================================
// REPLACE: Root Searcher (FIXED with PV Table)
// ========================================

Move search_position(Position& pos) {
    time_up = false;
    nodes_searched = 0;
    start_time = current_time_ms();
    
    // Clear PV table
    memset(pv_table, 0, sizeof(pv_table));
    memset(pv_length, 0, sizeof(pv_length));
    
    // DON'T clear position_history or halfmove_clock here!
    // They should persist across searches for repetition detection!
    
    Move best_move;
    bool found_move = false;
    int prev_score = 0;
    
    for (int depth = 1; depth <= MAX_DEPTH && !time_up; depth++) {
        int alpha, beta;
        
        // Aspiration windows (narrow search window for speed)
        int original_alpha, original_beta;
        if (depth >= 5) {
            original_alpha = prev_score - ASPIRATION_WINDOW;  // 0.5 pawn window
            original_beta = prev_score + ASPIRATION_WINDOW;
            alpha = original_alpha;
            beta = original_beta;
        } else {
            original_alpha = -INFINITY_SCORE;
            original_beta = INFINITY_SCORE;
            alpha = -INFINITY_SCORE;
            beta = INFINITY_SCORE;
        }
        
        // Generate moves ONCE before the loop
        MoveList moves = generate_legal_moves(pos);
        
        // Debug output removed from production code
        
        if (moves.moves.empty()) {
            // No moves available - game over
            std::cout << "info string No legal moves found - game over" << std::endl;
            break;
        }
        
        Move depth_best_move;
        int best_score = -INFINITY_SCORE;
        
        // Search each move WITHOUT modifying the loop
        // Sort moves at root for better move ordering
        Move tt_move;
        int dummy_score;
        probe_tt(pos.hash_key, depth, alpha, beta, dummy_score, tt_move, 0);
        sort_moves_enhanced(pos, moves.moves, tt_move, 0);
        
        for (const auto& move : moves.moves) {
            // ADDED: Check time at root level
            if (current_time_ms() - start_time > time_limit) {
                time_up = true;
                break;
            }
            
            BoardState state = make_move(pos, move);
            int score = -pvs_search(pos, depth - 1, -beta, -alpha, 1, true);
            unmake_move(pos, move, state);  // Always unmake!

            if (time_up) break;

            if (score > best_score) {
                best_score = score;
                depth_best_move = move;
                found_move = true;
                
                // FIX: Update PV at root (ply 0)
                pv_table[0][0] = move;
                pv_length[0] = 1;
                
                // Copy PV from deeper plies
                for (int j = 0; j < pv_length[1] && j < MAX_PLY - 1; j++) {
                    pv_table[0][pv_length[0]] = pv_table[1][j];
                    pv_length[0]++;
                }
            }

            if (score > alpha) alpha = score;
        }
        
        // Re-search if outside aspiration window
        if (depth >= 5 && !time_up && (best_score <= original_alpha || best_score >= original_beta)) {
            // Re-search ALL moves with full window
            alpha = -INFINITY_SCORE;
            beta = INFINITY_SCORE;
            best_score = -INFINITY_SCORE;
            
            for (const auto& move : moves.moves) {
                BoardState state = make_move(pos, move);
                int score = -pvs_search(pos, depth - 1, -beta, -alpha, 1, true);
                unmake_move(pos, move, state);
                
                if (score > best_score) {
                    best_score = score;
                    depth_best_move = move;
                }
                if (score > alpha) alpha = score;
            }
            
            // Update PV after re-search - FIXED: Clear and rebuild PV properly
            if (depth_best_move.move != 0) {
                pv_table[0][0] = depth_best_move;
                pv_length[0] = 1;
                
                // Copy PV from deeper plies (same as normal search)
                for (int j = 0; j < pv_length[1] && j < MAX_PLY - 1; j++) {
                    pv_table[0][pv_length[0]] = pv_table[1][j];
                    pv_length[0]++;
                }
            }
        }
        
        if (!time_up && found_move) {
            best_move = depth_best_move;
            prev_score = best_score;
            
            // FIX: Stricter mate score detection
            // Only treat as mate if score is VERY close to MATE_SCORE
            // FIX: Only treat as mate if score is VERY close to MATE_SCORE
            if (best_score >= MATE_SCORE - 10) {
                int mate_in = (MATE_SCORE - best_score + 1) / 2;
                std::cout << "info depth " << depth << " score mate " << mate_in
                          << " nodes " << nodes_searched << " time " << (current_time_ms() - start_time)
                          << " pv ";
                for (int i = 0; i < pv_length[0] && i < 10; i++) {
                    print_move_uci(pv_table[0][i].move);
                    std::cout << " ";
                }
                std::cout << std::endl;
            } else if (best_score <= -MATE_SCORE + 100) {  // FIX: Much stricter threshold
                int mate_in = (MATE_SCORE + best_score) / 2;  // FIX: Correct calculation
                std::cout << "info depth " << depth << " score mate -" << mate_in
                          << " nodes " << nodes_searched << " time " << (current_time_ms() - start_time)
                          << " pv ";
                for (int i = 0; i < pv_length[0] && i < 10; i++) {
                    print_move_uci(pv_table[0][i].move);
                    std::cout << " ";
                }
                std::cout << std::endl;
            } else {
                // Normal centipawn score (using named constants)
                int clamped_score = best_score;
                if (clamped_score > EVAL_CLAMP_MAX) clamped_score = EVAL_CLAMP_MAX;
                if (clamped_score < EVAL_CLAMP_MIN) clamped_score = EVAL_CLAMP_MIN;
                std::cout << "info depth " << depth << " score cp " << clamped_score
                          << " nodes " << nodes_searched << " time " << (current_time_ms() - start_time)
                          << " pv ";
                for (int i = 0; i < pv_length[0] && i < 10; i++) {
                    print_move_uci(pv_table[0][i].move);
                    std::cout << " ";
                }
                std::cout << std::endl;
            }
        }
        
        // REMOVED: Don't stop searching on mate scores
        // This was causing the engine to resign prematurely
        // if (best_score >= MATE_SCORE - 10 || best_score <= -MATE_SCORE + 10) {
        //     break;
        // }
    }
    
    // Add PV fallback code if PV is empty but we found a move
    if (pv_length[0] == 0 && found_move) {
        // Manually set PV to best move
        pv_table[0][0] = best_move;
        pv_length[0] = 1;
    }
    
    return best_move;
}

// ========================================
// 13. Search (FIXED) - Keep for compatibility
// ========================================

// ========================================
// 14. UCI Protocol Implementation
// ========================================

// FEN Parsing
void parse_fen(Position& pos, const std::string& fen) {
    // Clear position
    memset(&pos, 0, sizeof(Position));
    pos.en_passant_square = -1;
    
    size_t idx = 0;
    int square = 0; // Start at a8 (0 in a8=0 system)
    
    // Parse piece placement
    int rank = 0, file = 0;  // Start at rank 0 (a8), file 0
    while (idx < fen.length() && fen[idx] != ' ') {
        char c = fen[idx++];
        
        if (c == '/') {
            rank++;  // Move to next rank
            file = 0;  // Reset file
        } else if (isdigit(c)) {
            file += (c - '0');  // Skip empty squares
        } else {
            int square = rank * 8 + file;
            int color = isupper(c) ? WHITE : BLACK;
            c = tolower(c);
            
            int piece = -1;
            if (c == 'p') piece = P;
            else if (c == 'n') piece = N;
            else if (c == 'b') piece = B;
            else if (c == 'r') piece = R;
            else if (c == 'q') piece = Q;
            else if (c == 'k') piece = K;
            
            if (piece != -1) {
                set_bit(pos.pieces[color][piece], square);
                set_bit(pos.occupancies[color], square);
                set_bit(pos.occupancies[2], square);
            }
            file++;
        }
    }
    
    // Parse side to move
    idx++; // Skip space
    pos.side_to_move = (fen[idx] == 'w') ? WHITE : BLACK;
    idx += 2; // Skip side and space
    
    // Parse castling rights
    pos.castling_rights = 0;
    while (idx < fen.length() && fen[idx] != ' ') {
        if (fen[idx] == 'K') pos.castling_rights |= 1;
        if (fen[idx] == 'Q') pos.castling_rights |= 2;
        if (fen[idx] == 'k') pos.castling_rights |= 4;
        if (fen[idx] == 'q') pos.castling_rights |= 8;
        idx++;
    }
    idx++; // Skip space
    
    // Parse en passant
    if (fen[idx] != '-') {
        int file = fen[idx] - 'a';
        int rank = 8 - (fen[idx + 1] - '0');
        pos.en_passant_square = rank * 8 + file;
    }
    
    pos.hash_key = generate_hash_key(pos);
}

// Move Parsing and UCI Integration
Move parse_move(Position& pos, const std::string& move_str) {
    if (move_str.length() < 4) return Move();
    
    int from_file = move_str[0] - 'a';
    int from_rank = '8' - move_str[1];  // Convert UCI rank to a8=0 index
    int from = from_rank * 8 + from_file;
    
    int to_file = move_str[2] - 'a';
    int to_rank = '8' - move_str[3];    // Convert UCI rank to a8=0 index
    int to = to_rank * 8 + to_file;
    
    // Find piece type
    int piece = -1;
    for (int p = 0; p < 6; p++) {
        if (get_bit(pos.pieces[pos.side_to_move][p], from)) {
            piece = p;
            break;
        }
    }
    
    // Check for promotion
    int promo = 0;
    if (move_str.length() == 5) {
        char p = tolower(move_str[4]);
        if (p == 'q') promo = Q;
        else if (p == 'r') promo = R;
        else if (p == 'b') promo = B;
        else if (p == 'n') promo = N;
    }
    
    // Generate all legal moves and find matching one
    MoveList moves = generate_legal_moves(pos);
    for (const auto& m : moves.moves) {
        if (m.get_from() == from && m.get_to() == to && m.get_promo() == promo) {
            return m;
        }
    }
    
    return Move();
}

std::string move_to_string(const Move& move) {
    std::string result;
    
    // CRITICAL FIX: Convert a8=0 coordinates to UCI a1=0 coordinates
    int from = move.get_from();
    int to = move.get_to();
    
    // File stays the same, only flip rank
    result += ('a' + (from % 8));
    result += ('8' - (from / 8));  // Flip rank
    result += ('a' + (to % 8));
    result += ('8' - (to / 8));    // Flip rank
    
    if (move.get_promo() != 0) {
        // Add bounds check to prevent out-of-bounds access
        if (move.get_promo() > 0 && move.get_promo() < 5) {
            const char promos[] = {' ', 'n', 'b', 'r', 'q'};
            result += promos[move.get_promo()];
        }
    }
    
    return result;
}

// Print move function for UCI output
void print_move(const Move& move) {
    std::cout << move_to_string(move);
}

// Print move list function
void print_move_list(const MoveList& move_list) {
    for (size_t i = 0; i < move_list.moves.size(); i++) {
        print_move(move_list.moves[i]);
        if (i < move_list.moves.size() - 1) {
            std::cout << " ";
        }
    }
}

// UCI command handling (FIXED)
void uci_loop() {
    std::string command;
    Position current_pos;
    setup_starting_position(current_pos); // Initialize with starting position
    
    while (std::getline(std::cin, command)) {
        if (command == "uci") {
            std::cout << "id name Douchess" << std::endl;
            std::cout << "id author changcheng967" << std::endl;
            std::cout << "uciok" << std::endl;
        }
        else if (command == "isready") {
            std::cout << "readyok" << std::endl;
        }
        else if (command == "ucinewgame") {
            clear_tt();
            clear_history();
            position_history.clear();  // ADD THIS!
            halfmove_clock = 0;         // ADD THIS!
            setup_starting_position(current_pos);
            // ADD: Verify TT is actually cleared
            std::cout << "info string TT cleared, " << TT_SIZE << " entries reset" << std::endl;
        }
        else if (command.substr(0, 8) == "position") {
            // ✅ CLEAR HISTORY WHEN SETTING NEW POSITION!
            position_history.clear();
            halfmove_clock = 0;
            
            if (command.find("startpos") != std::string::npos) {
                setup_starting_position(current_pos);
                std::cout << "info string Position set to startpos" << std::endl;
                
                // Handle moves after startpos
                size_t moves_pos = command.find("moves");
                if (moves_pos != std::string::npos) {
                    std::istringstream iss(command.substr(moves_pos + 6));
                    std::string move_str;
                    while (iss >> move_str) {
                        Move m = parse_move(current_pos, move_str);
                        if (m.move != 0) {
                            BoardState state = make_move(current_pos, m);
                            // Don't unmake - we're applying the move permanently
                        } else {
                            std::cout << "info string Invalid move: " << move_str << std::endl;
                            break;
                        }
                    }
                }
            }
            // Handle FEN positions
            else if (command.find("fen") != std::string::npos) {
                size_t fen_start = command.find("fen") + 4;
                size_t moves_pos = command.find("moves");
                std::string fen;
                if (moves_pos != std::string::npos) {
                    fen = command.substr(fen_start, moves_pos - fen_start);
                } else {
                    fen = command.substr(fen_start);
                }
                parse_fen(current_pos, fen);
                std::cout << "info string Position set from FEN" << std::endl;
                
                // Handle moves after FEN
                if (moves_pos != std::string::npos) {
                    std::istringstream iss(command.substr(moves_pos + 6));
                    std::string move_str;
                    while (iss >> move_str) {
                        Move m = parse_move(current_pos, move_str);
                        if (m.move != 0) {
                            BoardState state = make_move(current_pos, m);
                        } else {
                            std::cout << "info string Invalid move: " << move_str << std::endl;
                            break;
                        }
                    }
                }
            }
        }
        else if (command.substr(0, 2) == "go") {
            // Parse go command parameters
            std::istringstream iss(command);
            std::string token;
            iss >> token; // Skip "go"
            
            // STRICT 2-SECOND LIMIT: Always use 2000ms
            time_limit = 2000;
            
            // Parse time control parameters (for logging/future use)
            int wtime = 0, btime = 0, winc = 0, binc = 0;
            int movestogo = 40;
            bool use_movetime = false;
            
            while (iss >> token) {
                if (token == "wtime") {
                    iss >> wtime;
                } else if (token == "winc") {
                    iss >> winc;
                } else if (token == "btime") {
                    iss >> btime;
                } else if (token == "binc") {
                    iss >> binc;
                } else if (token == "movetime") {
                    iss >> time_limit;
                    use_movetime = true;
                    // Cap movetime at 2000ms
                    if (time_limit > 2000) time_limit = 2000;
                } else if (token == "depth") {
                    int depth;
                    iss >> depth;
                    time_limit = 2000;  // Still respect 2-second limit
                    use_movetime = true;
                } else if (token == "infinite") {
                    // ✅ ENFORCE: "infinite" limited to 2000ms
                    time_limit = 2000;
                    use_movetime = true;
                } else if (token == "movestogo") {
                    iss >> movestogo;
                }
            }
            
            // If not using movetime/depth/infinite, enforce 950ms limit
            if (!use_movetime) {
                time_limit = 2000;
            }
            
            Move best = search_position(current_pos);
            
            if (best.move != 0) {
                std::cout << "bestmove ";
                print_move_uci(best.move);
                std::cout << std::endl;
            } else {
                // No move found - output any legal move
                MoveList moves = generate_legal_moves(current_pos);
                if (!moves.moves.empty()) {
                    std::cout << "bestmove ";
                    print_move_uci(moves.moves[0].move);
                    std::cout << std::endl;
                } else {
                    std::cout << "bestmove 0000" << std::endl;
                }
            }
        }
        else if (command == "quit") {
            break;
        }
        else if (command == "d" || command == "display") {
            // Display current position (for debugging)
            std::cout << "info string Current position hash: " << current_pos.hash_key << std::endl;
        }
    }
}

// ========================================
// 15. Main Function
// ========================================
int main() {
    // Initialize all systems
    init_zobrist_keys();
    init_attack_tables();
    clear_tt(); // Initialize TT
    
    // Start UCI mode
    uci_loop();
    
    return 0;
}

