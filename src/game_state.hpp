#pragma once
#ifndef GAME_STATE_HPP
#define GAME_STATE_HPP

#include <cstdint>
#include <vector>
#include <string>
#include <utility>

// ============================================================================
// Enums
// ============================================================================

enum class Suit : int {
    CLUBS = 0,
    DIAMONDS = 1,
    HEARTS = 2,
    SPADES = 3
};

enum class Rank : int {
    ACE = 0, TWO = 1, THREE = 2, FOUR = 3, FIVE = 4,
    SIX = 5, SEVEN = 6, EIGHT = 7, NINE = 8, TEN = 9,
    JACK = 10, QUEEN = 11, KING = 12
};

enum class PileType : int {
    STOCK = 0,
    WASTE = 1,
    FOUNDATION_0 = 2,
    FOUNDATION_1 = 3,
    FOUNDATION_2 = 4,
    FOUNDATION_3 = 5,
    TABLEAU_0 = 6,
    TABLEAU_1 = 7,
    TABLEAU_2 = 8,
    TABLEAU_3 = 9,
    TABLEAU_4 = 10,
    TABLEAU_5 = 11,
    TABLEAU_6 = 12
};

enum class MoveType : int {
    DRAW_STOCK = 0,
    WASTE_TO_FOUNDATION = 1,
    WASTE_TO_TABLEAU = 2,
    TABLEAU_TO_FOUNDATION = 3,
    TABLEAU_TO_TABLEAU = 4,
    RECYCLE_WASTE = 5
};

// ============================================================================
// Card
// ============================================================================

struct Card {
    int card_id;     // 0-51
    bool face_down;
    int x;
    int y;

    Card() : card_id(0), face_down(true), x(0), y(0) {}
    Card(int id, bool down, int X = 0, int Y = 0)
        : card_id(id), face_down(down), x(X), y(Y) {}

    Suit suit() const { return Suit(card_id % 4); }
    Rank rank() const { return Rank(card_id / 4); }
    bool is_red() const { int s = card_id % 4; return s == 1 || s == 2; }
    bool is_black() const { return !is_red(); }

    std::string display_str() const;
    std::string full_name() const;
    std::string to_string() const;

    static Card from_memory(uint16_t word, int X = 0, int Y = 0);
    Card clone() const { return Card(card_id, face_down, x, y); }
    bool operator==(const Card& other) const { return card_id == other.card_id; }
};

// ============================================================================
// Pile
// ============================================================================

struct Pile {
    PileType pile_type;
    std::vector<Card> cards;
    int x = 0;
    int y = 0;

    explicit Pile(PileType t) : pile_type(t) {}
    Pile(const Pile& other) = default;

    bool is_empty() const { return cards.empty(); }
    const Card* top_card() const;
    int face_down_count() const;
    bool is_stock() const;
    bool is_waste() const;
    bool is_foundation() const;
    bool is_tableau() const;
    int tableau_index() const;
    int foundation_index() const;
    Pile clone() const;

    std::string to_string() const;
};

// ============================================================================
// Move
// ============================================================================

struct Move {
    MoveType move_type;
    PileType source;
    PileType dest;
    int card_id = -1;   // -1 = none/unknown
    int num_cards = 1;
    int priority = 0;

    Move() {}
    Move(MoveType mt, PileType src, PileType dst,
         int cid = -1, int n = 1, int p = 0)
        : move_type(mt), source(src), dest(dst), card_id(cid), num_cards(n), priority(p) {}

    std::string to_string() const;

};

// ============================================================================
// GameState
// ============================================================================

struct GameState {
    Pile stock;
    Pile waste;
    std::vector<Pile> foundations;  // 4 piles
    std::vector<Pile> tableau;       // 7 piles
    int draw_count = 1;              // 1 or 3

    GameState();
    GameState(const GameState& other) = default;
    GameState(GameState&& other) noexcept = default;
    GameState& operator=(const GameState&) = default;
    GameState& operator=(GameState&&) noexcept = default;

    GameState clone() const;
    int64_t state_hash() const;
    int64_t tableau_hash() const;

    bool is_won() const;
    int total_cards() const;
    int foundation_count() const;

    const Pile* foundation_for_suit(Suit s) const;
    const Pile* foundation_accepts(const Card& card) const;
    std::vector<const Pile*> tableau_accepts(const Card& card) const;

    GameState& apply_move(const Move& move);

    std::string display() const;

private:
    Pile* _pile_by_type(PileType pt);
    const Pile* _pile_by_type(PileType pt) const;
};

#endif // GAME_STATE_HPP
