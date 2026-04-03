#include "game_state.hpp"
#include <sstream>
#include <iomanip>
#include <algorithm>

using namespace std;

// ============================================================================
// Card
// ============================================================================

static const char* RANK_DISPLAY = "A23456789TJQK";
static const char* SUIT_SYMBOL = "\x03\x04\x05\x06";  // ♣ ♦ ♥ ♠
static const char* RANK_NAME[] = {
    "ace","two","three","four","five","six","seven","eight",
    "nine","ten","jack","queen","king"
};
static const char* SUIT_NAME[] = {
    "clubs","diamonds","hearts","spades"
};

string Card::display_str() const {
    if (face_down) return "[?]";
    int r = card_id / 4;
    int s = card_id % 4;
    string out;
    out += RANK_DISPLAY[r];
    out += SUIT_SYMBOL[s];
    return out;
}

string Card::full_name() const {
    if (face_down) return "face-down";
    Rank r = rank();
    Suit s = suit();
    return string(RANK_NAME[static_cast<int>(r)]) + " of " + SUIT_NAME[static_cast<int>(s)];
}

string Card::to_string() const {
    return display_str();
}

Card Card::from_memory(uint16_t word, int X, int Y) {
    int id = word & 0x3F;
    bool down = !(word & 0x8000);
    return Card(id, down, X, Y);
}

// ============================================================================
// Pile
// ============================================================================

const Card* Pile::top_card() const {
    if (cards.empty()) return nullptr;
    return &cards.back();
}

int Pile::face_down_count() const {
    int count = 0;
    for (const auto& c : cards) if (c.face_down) count++;
    return count;
}

bool Pile::is_stock()    const { return pile_type == PileType::STOCK; }
bool Pile::is_waste()    const { return pile_type == PileType::WASTE; }
bool Pile::is_foundation() const {
    return PileType::FOUNDATION_0 <= pile_type && pile_type <= PileType::FOUNDATION_3;
}
bool Pile::is_tableau() const {
    return PileType::TABLEAU_0 <= pile_type && pile_type <= PileType::TABLEAU_6;
}

int Pile::tableau_index() const {
    return static_cast<int>(pile_type) - static_cast<int>(PileType::TABLEAU_0);
}

int Pile::foundation_index() const {
    return static_cast<int>(pile_type) - static_cast<int>(PileType::FOUNDATION_0);
}

Pile Pile::clone() const {
    Pile p(pile_type);
    p.cards.reserve(cards.size());
    for (const auto& c : cards) p.cards.push_back(c.clone());
    p.x = x; p.y = y;
    return p;
}

string Pile::to_string() const {
    ostringstream ss;
    if (is_foundation()) {
        ss << "Foundation " << foundation_index();
    } else if (is_tableau()) {
        ss << "Tableau " << tableau_index();
    } else if (is_stock()) {
        ss << "Stock";
    } else {
        ss << "Waste";
    }
    ss << ": [";
    for (const auto& c : cards) ss << c.to_string() << " ";
    ss << "]";
    return ss.str();
}

// ============================================================================
// Move
// ============================================================================

string Move::to_string() const {
    ostringstream ss;
    string card_str = card ? card->to_string() : "?";
    switch (move_type) {
        case MoveType::DRAW_STOCK:
            return "Draw from stock";
        case MoveType::WASTE_TO_FOUNDATION:
            return "Waste -> Foundation: " + card_str;
        case MoveType::WASTE_TO_TABLEAU:
            return "Waste -> Tableau " + std::to_string(static_cast<int>(dest) - static_cast<int>(PileType::TABLEAU_0))
                   + ": " + card_str;
        case MoveType::TABLEAU_TO_FOUNDATION:
            return "Tableau " + std::to_string(static_cast<int>(source) - static_cast<int>(PileType::TABLEAU_0))
                   + " -> Foundation: " + card_str;
        case MoveType::TABLEAU_TO_TABLEAU:
            return "Tableau " + std::to_string(static_cast<int>(source) - static_cast<int>(PileType::TABLEAU_0))
                   + " -> Tableau " + std::to_string(static_cast<int>(dest) - static_cast<int>(PileType::TABLEAU_0))
                   + ": " + card_str + " (" + std::to_string(num_cards) + " cards)";
        case MoveType::RECYCLE_WASTE:
            return "Recycle waste -> stock";
        default:
            return "Unknown move";
    }
}

// ============================================================================
// GameState
// ============================================================================

GameState::GameState()
    : stock(PileType::STOCK)
    , waste(PileType::WASTE)
{
    foundations.reserve(4);
    for (int i = 0; i < 4; i++)
        foundations.emplace_back(static_cast<PileType>(static_cast<int>(PileType::FOUNDATION_0) + i));
    tableau.reserve(7);
    for (int i = 0; i < 7; i++)
        tableau.emplace_back(static_cast<PileType>(static_cast<int>(PileType::TABLEAU_0) + i));
}

GameState GameState::clone() const {
    GameState s;
    s.stock = stock.clone();
    s.waste = waste.clone();
    s.foundations.clear();
    for (const auto& f : foundations) s.foundations.push_back(f.clone());
    s.tableau.clear();
    for (const auto& t : tableau) s.tableau.push_back(t.clone());
    s.draw_count = draw_count;
    return s;
}

static int64_t hash_combine(int64_t h, int64_t v) {
    return h ^ (v + 0x9e3779b97f4a7c15LL + (h << 6) + (h >> 2));
}

int64_t GameState::state_hash() const {
    int64_t h = 0;
    auto hash_pile = [&](const Pile& p) {
        for (const auto& c : p.cards) {
            h = hash_combine(h, static_cast<int64_t>(c.card_id) | (c.face_down ? 0x8000LL : 0));
        }
        h = hash_combine(h, 0xABCDEF);
    };
    hash_pile(stock);
    hash_pile(waste);
    for (const auto& f : foundations) hash_pile(f);
    for (const auto& t : tableau) hash_pile(t);
    return h;
}

int64_t GameState::tableau_hash() const {
    int64_t h = 0;
    auto hash_pile = [&](const Pile& p) {
        for (const auto& c : p.cards) {
            h = hash_combine(h, static_cast<int64_t>(c.card_id) | (c.face_down ? 0x8000LL : 0));
        }
        h = hash_combine(h, 0xABCDEF);
    };
    for (const auto& f : foundations) hash_pile(f);
    for (const auto& t : tableau) hash_pile(t);
    return h;
}

bool GameState::is_won() const {
    for (const auto& f : foundations)
        if ((int)f.cards.size() != 13) return false;
    return true;
}

int GameState::total_cards() const {
    int total = 0;
    for (const auto& p : foundations) total += (int)p.cards.size();
    for (const auto& t : tableau) total += (int)t.cards.size();
    total += (int)stock.cards.size();
    total += (int)waste.cards.size();
    return total;
}

int GameState::foundation_count() const {
    int total = 0;
    for (const auto& f : foundations) total += (int)f.cards.size();
    return total;
}

Pile* GameState::_pile_by_type(PileType pt) {
    if (pt == PileType::STOCK) return &stock;
    if (pt == PileType::WASTE) return &waste;
    if (PileType::FOUNDATION_0 <= pt && pt <= PileType::FOUNDATION_3)
        return &foundations[static_cast<int>(pt) - static_cast<int>(PileType::FOUNDATION_0)];
    if (PileType::TABLEAU_0 <= pt && pt <= PileType::TABLEAU_6)
        return &tableau[static_cast<int>(pt) - static_cast<int>(PileType::TABLEAU_0)];
    return nullptr;
}

const Pile* GameState::_pile_by_type(PileType pt) const {
    if (pt == PileType::STOCK) return &stock;
    if (pt == PileType::WASTE) return &waste;
    if (PileType::FOUNDATION_0 <= pt && pt <= PileType::FOUNDATION_3)
        return &foundations[static_cast<int>(pt) - static_cast<int>(PileType::FOUNDATION_0)];
    if (PileType::TABLEAU_0 <= pt && pt <= PileType::TABLEAU_6)
        return &tableau[static_cast<int>(pt) - static_cast<int>(PileType::TABLEAU_0)];
    return nullptr;
}

const Pile* GameState::foundation_for_suit(Suit s) const {
    for (const auto& f : foundations) {
        if (!f.cards.empty() && f.cards[0].suit() == s) return &f;
    }
    for (const auto& f : foundations) if (f.is_empty()) return &f;
    return nullptr;
}

const Pile* GameState::foundation_accepts(const Card& card) const {
    for (const auto& f : foundations) {
        if (f.is_empty()) {
            if (card.rank() == Rank::ACE) return &f;
        } else {
            const Card* top = f.top_card();
            if (top && top->suit() == card.suit()
                && top->rank() == Rank(static_cast<int>(card.rank()) - 1))
                return &f;
        }
    }
    return nullptr;
}

vector<const Pile*> GameState::tableau_accepts(const Card& card) const {
    vector<const Pile*> result;
    for (const auto& t : tableau) {
        if (t.is_empty()) {
            if (card.rank() == Rank::KING) result.push_back(&t);
        } else {
            const Card* top = t.top_card();
            if (top && !top->face_down && top->is_red() != card.is_red()
                && top->rank() == Rank(static_cast<int>(card.rank()) + 1))
                result.push_back(&t);
        }
    }
    return result;
}

GameState& GameState::apply_move(const Move& move) {
    switch (move.move_type) {
        case MoveType::DRAW_STOCK: {
            int n = min(draw_count, (int)stock.cards.size());
            for (int i = 0; i < n; i++) {
                Card c = stock.cards.back();
                stock.cards.pop_back();
                c.face_down = false;
                waste.cards.push_back(c);
            }
            break;
        }
        case MoveType::RECYCLE_WASTE: {
            while (!waste.cards.empty()) {
                Card c = waste.cards.back();
                waste.cards.pop_back();
                c.face_down = true;
                stock.cards.push_back(c);
            }
            break;
        }
        case MoveType::WASTE_TO_FOUNDATION: {
            if (!waste.cards.empty()) {
                Card c = waste.cards.back();
                waste.cards.pop_back();
                c.face_down = false;
                _pile_by_type(move.dest)->cards.push_back(c);
            }
            break;
        }
        case MoveType::WASTE_TO_TABLEAU: {
            if (!waste.cards.empty()) {
                Card c = waste.cards.back();
                waste.cards.pop_back();
                c.face_down = false;
                _pile_by_type(move.dest)->cards.push_back(c);
            }
            break;
        }
        case MoveType::TABLEAU_TO_FOUNDATION: {
            Pile* src = _pile_by_type(move.source);
            if (!src->cards.empty()) {
                Card c = src->cards.back();
                src->cards.pop_back();
                c.face_down = false;
                if (!src->cards.empty() && src->cards.back().face_down) {
                    src->cards.back().face_down = false;
                }
                _pile_by_type(move.dest)->cards.push_back(c);
            }
            break;
        }
        case MoveType::TABLEAU_TO_TABLEAU: {
            Pile* src = _pile_by_type(move.source);
            Pile* dst = _pile_by_type(move.dest);
            int start = (int)src->cards.size() - move.num_cards;
            for (int i = 0; i < move.num_cards; i++) {
                Card c = src->cards[start + i];
                c.face_down = false;
                dst->cards.push_back(c);
            }
            src->cards.erase(src->cards.begin() + start, src->cards.end());
            if (!src->cards.empty() && src->cards.back().face_down) {
                src->cards.back().face_down = false;
            }
            break;
        }
    }
    return *this;
}

string GameState::display() const {
    ostringstream ss;
    ss << "============================================================\n";

    string stock_str = stock.is_empty() ? "[_]" : "[" + to_string((int)stock.cards.size()) + "]";
    string waste_str = waste.top_card() ? waste.top_card()->display_str() : "[_]";

    ss << "Stock: " << stock_str << " (" << stock.cards.size() << " cards)  "
       << "Waste: " << waste_str << " (" << waste.cards.size() << " cards)  ";

    vector<string> found_strs;
    for (const auto& f : foundations)
        found_strs.push_back(f.top_card() ? f.top_card()->display_str() : "[_]");
    ss << "Foundations: " << found_strs[0] << " " << found_strs[1] << " "
       << found_strs[2] << " " << found_strs[3] << "\n";
    ss << "------------------------------------------------------------\n";

    int max_h = 0;
    for (const auto& t : tableau)
        max_h = max(max_h, (int)t.cards.size());

    for (int row = 0; row < max_h; row++) {
        for (const auto& t : tableau) {
            if (row < (int)t.cards.size()) {
                const Card& c = t.cards[row];
                ss << setw(5) << c.to_string();
            } else {
                ss << "     ";
            }
            ss << "  ";
        }
        ss << "\n";
    }

    if (max_h == 0) ss << "  (all tableau columns empty)\n";
    ss << "============================================================\n";
    return ss.str();
}
