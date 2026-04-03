#include "solver.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>

using namespace std;

// ============================================================================
// PerfectSolver
// ============================================================================

PerfectSolver::PerfectSolver(double timeout, int max_stock_passes, bool verbose)
    : timeout_(timeout), max_stock_passes_(max_stock_passes), verbose_(verbose) {}

bool PerfectSolver::timed_out() const {
    return (clock() - start_time_) / (double)CLOCKS_PER_SEC >= timeout_;
}

int PerfectSolver::foundation_count(const GameState& state) const {
    return state.foundation_count();
}

const Move* PerfectSolver::get_next_move() {
    if (current_index_ >= (int)move_queue_.size()) return nullptr;
    return &move_queue_[current_index_++];
}

SolveResult PerfectSolver::solve(const GameState& initial_state) {
    nodes_explored_ = 0;
    start_time_ = clock();
    visited_.clear();
    move_queue_.clear();
    current_index_ = 0;
    solved_flag_ = false;

    // Apply forced moves (Aces, safe Twos)
    GameState state = initial_state.clone();
    vector<Move> pre_moves = apply_forced_moves(state);

    if (verbose_) {
        cerr << "  [dbg] forced pre-moves: " << pre_moves.size() << "\n";
        cerr << "  [dbg] foundation_count=" << foundation_count(state)
             << "  is_won=" << state.is_won() << "\n";
        if (pre_moves.empty()) {
            cerr << "  [dbg] initial state:\n" << state.display();
        }
    }

    if (state.is_won()) {
        double elapsed = (clock() - start_time_) / (double)CLOCKS_PER_SEC;
        move_queue_ = std::move(pre_moves);
        solved_flag_ = true;
        return SolveResult(true, std::move(move_queue_), nodes_explored_, elapsed);
    }

    // Run DFS
    int tt_skipped = 0;
    int won_reached = 0;
    vector<Move> result = dfs(state, 0, 0, nullptr, &tt_skipped, &won_reached);
    double elapsed = (clock() - start_time_) / (double)CLOCKS_PER_SEC;

    if (verbose_) {
        cerr << "  [dbg] DFS done: nodes=" << nodes_explored_
             << "  tt_skipped=" << tt_skipped
             << "  won_reached=" << won_reached << "\n";
    }

    if (!result.empty()) {
        // Sentinel = won during DFS; pre_moves were applied to the clone.
        // Add pre_moves to move_queue_ so execute_solution can verify the
        // actual game state matches (the game's built-in auto-move will handle
        // them, so they'll either succeed or be no-ops).
        if (result.size() == 1 && result[0].is_no_move()) {
            move_queue_ = std::move(pre_moves);
            solved_flag_ = true;
            return SolveResult(true, move_queue_, nodes_explored_, elapsed);
        }
        vector<Move> all_moves = pre_moves;
        all_moves.insert(all_moves.end(), result.begin(), result.end());
        vector<Move> optimized = remove_cycles(initial_state, std::move(all_moves));
        if (verbose_ && (int)optimized.size() < (int)all_moves.size()) {
            cerr << "  > Optimizer: removed " << (int)all_moves.size() - (int)optimized.size()
                 << " redundant moves\n";
        }
        move_queue_ = std::move(optimized);
        solved_flag_ = true;
        return SolveResult(true, std::move(move_queue_), nodes_explored_, elapsed);
    }

    string reason = timed_out() ? "timeout" : "exhausted search space";
    return SolveResult(false, nodes_explored_, elapsed, reason);
}

vector<Move> PerfectSolver::dfs(GameState state, int depth, int stock_passes,
                                  const vector<Move>* recent_tab_moves,
                                  int* tt_skipped, int* won_reached) const {
    nodes_explored_++;

    if (nodes_explored_ % 500000 == 0 && verbose_) {
        cerr << "  [dbg] nodes=" << nodes_explored_ << " depth=" << depth << "\n";
    }

    if (nodes_explored_ % 2000 == 0 && timed_out())
        return {};

    if (depth > 800) return {};

    if (state.is_won()) {
        if (won_reached) (*won_reached)++;
        // Return sentinel so the call chain knows we won (empty vec = "no path found")
        return {Move(MoveType::NONE, PileType::STOCK, PileType::STOCK)};
    }

    // Transposition check
    int64_t h = state.state_hash();
    int fc = foundation_count(state);
    auto it = visited_.find(h);
    if (it != visited_.end() && it->second >= fc) {
        if (tt_skipped) (*tt_skipped)++;
        return {};
    }
    visited_[h] = fc;

    vector<Move> moves = generate_ordered_moves(state, stock_passes);

    if (recent_tab_moves) {
        vector<Move> filtered;
        for (const auto& m : moves) {
            if (!is_reverse_of_recent(m, *recent_tab_moves))
                filtered.push_back(m);
        }
        if (!filtered.empty()) moves.swap(filtered);
    }

    for (const Move& move : moves) {
        GameState new_state = state.clone();
        new_state.apply_move(move);

        vector<Move> forced = apply_forced_moves(new_state);

        int new_passes = stock_passes;
        if (move.move_type == MoveType::RECYCLE_WASTE) {
            new_passes++;
            if (new_passes > max_stock_passes_) continue;
        }

        // Update recent tableau moves
        vector<Move> new_recent;
        if (!forced.empty()) {
            new_recent.clear();
        } else if (move.move_type == MoveType::TABLEAU_TO_TABLEAU) {
            new_recent = recent_tab_moves ? *recent_tab_moves : vector<Move>();
            new_recent.push_back(move);
            if ((int)new_recent.size() > 6) new_recent.erase(new_recent.begin());
        } else {
            new_recent = recent_tab_moves ? *recent_tab_moves : vector<Move>();
        }

        vector<Move> result = dfs(new_state, depth + 1, new_passes, &new_recent, tt_skipped, won_reached);
        if (!result.empty()) {
            // If result is the sentinel, propagate it up unchanged
            if (result.size() == 1 && result[0].is_no_move()) return result;
            vector<Move> full;
            full.push_back(move);
            full.insert(full.end(), forced.begin(), forced.end());
            full.insert(full.end(), result.begin(), result.end());
            return full;
        }

        if (timed_out()) return {};
    }

    return {};
}

bool PerfectSolver::is_reverse_of_recent(const Move& move, const vector<Move>& recent_tab) const {
    if (move.move_type != MoveType::TABLEAU_TO_TABLEAU) return false;
    for (const Move& prev : recent_tab) {
        if (move.source == prev.dest && move.dest == prev.source
            && move.num_cards == prev.num_cards)
            return true;
    }
    return false;
}

vector<Move> PerfectSolver::apply_forced_moves(GameState& state) const {
    vector<Move> forced;
    bool changed = true;

    while (changed) {
        changed = false;

        // Check waste
        if (!state.waste.cards.empty() && !state.waste.top_card()->face_down) {
            const Card* card = state.waste.top_card();
            if (is_auto_foundation_card(*card, state)) {
                const Pile* dest = state.foundation_accepts(*card);
                if (dest) {
                    Move m(MoveType::WASTE_TO_FOUNDATION, PileType::WASTE, dest->pile_type, card->card_id);
                    state.apply_move(m);
                    forced.push_back(m);
                    changed = true;
                    continue;
                }
            }
        }

        // Check tableau
        for (auto& t : state.tableau) {
            if (t.is_empty()) continue;
            const Card* card = t.top_card();
            if (card && !card->face_down && is_auto_foundation_card(*card, state)) {
                const Pile* dest = state.foundation_accepts(*card);
                if (dest) {
                    Move m(MoveType::TABLEAU_TO_FOUNDATION, t.pile_type, dest->pile_type, card->card_id);
                    state.apply_move(m);
                    forced.push_back(m);
                    changed = true;
                    break;
                }
            }
        }
    }
    return forced;
}

bool PerfectSolver::is_auto_foundation_card(const Card& card, const GameState& state) const {
    if (!state.foundation_accepts(card)) return false;
    int rank_val = static_cast<int>(card.rank());
    if (rank_val <= 1) return true;

    int min_opp = 99;
    int opp_count = 0;
    for (const auto& f : state.foundations) {
        if (!f.cards.empty()) {
            const Card* top = f.top_card();
            bool f_is_red = top->is_red();
            if ((card.is_red() && !f_is_red) || (!card.is_red() && f_is_red)) {
                opp_count++;
                min_opp = min(min_opp, static_cast<int>(top->rank()));
            }
        }
    }
    if (opp_count < 2) return false;
    return min_opp >= rank_val - 1;
}

vector<Move> PerfectSolver::generate_ordered_moves(const GameState& state, int stock_passes) const {
    vector<pair<int, Move>> moves;

    // Tableau -> Foundation
    for (const auto& t : state.tableau) {
        if (t.is_empty()) continue;
        const Card* card = t.top_card();
        if (card && !card->face_down) {
            if (const Pile* dest = state.foundation_accepts(*card)) {
                bool exposes = (int)t.cards.size() > 1 && t.cards[t.cards.size() - 2].face_down;
                int priority = exposes ? 900 : 800;
                moves.emplace_back(priority, Move(MoveType::TABLEAU_TO_FOUNDATION,
                                                  t.pile_type, dest->pile_type, card->card_id));
            }
        }
    }

    // Waste -> Foundation
    if (!state.waste.cards.empty() && !state.waste.top_card()->face_down) {
        const Card* card = state.waste.top_card();
        if (const Pile* dest = state.foundation_accepts(*card)) {
            moves.emplace_back(750, Move(MoveType::WASTE_TO_FOUNDATION,
                                          PileType::WASTE, dest->pile_type, card->card_id));
        }
    }

    // Tableau -> Tableau
    auto tab_moves = tableau_to_tableau_moves(state);
    for (auto& p : tab_moves) moves.emplace_back(p.first, std::move(p.second));

    // Waste -> Tableau
    if (!state.waste.cards.empty() && !state.waste.top_card()->face_down) {
        const Card* card = state.waste.top_card();
        for (const auto& dst : state.tableau) {
            bool can_place = false;
            if (dst.is_empty()) {
                if (card->rank() == Rank::KING) can_place = true;
            } else {
                const Card* top = dst.top_card();
                if (top && !top->face_down && top->is_red() != card->is_red()
                    && top->rank() == Rank(static_cast<int>(card->rank()) + 1))
                    can_place = true;
            }
            if (can_place) {
                moves.emplace_back(300, Move(MoveType::WASTE_TO_TABLEAU,
                                               PileType::WASTE, dst.pile_type, card->card_id));
            }
        }
    }

    // Draw from stock
    if (!state.stock.cards.empty()) {
        moves.emplace_back(100, Move(MoveType::DRAW_STOCK, PileType::STOCK, PileType::WASTE));
    }

    // Recycle waste
    if (state.stock.cards.empty() && !state.waste.cards.empty()) {
        if (stock_passes < max_stock_passes_) {
            moves.emplace_back(50, Move(MoveType::RECYCLE_WASTE, PileType::WASTE, PileType::STOCK));
        }
    }

    sort(moves.begin(), moves.end(),
         [](const pair<int, Move>& a, const pair<int, Move>& b) { return a.first > b.first; });

    vector<Move> result;
    for (auto& p : moves) result.push_back(std::move(p.second));
    return result;
}

vector<pair<int, Move>> PerfectSolver::tableau_to_tableau_moves(const GameState& state) const {
    vector<pair<int, Move>> moves;

    for (const auto& src : state.tableau) {
        if (src.is_empty()) continue;

        int face_up_start = -1;
        for (int i = 0; i < (int)src.cards.size(); i++) {
            if (!src.cards[i].face_down) { face_up_start = i; break; }
        }
        if (face_up_start == -1) continue;

        for (int start_idx = face_up_start; start_idx < (int)src.cards.size(); start_idx++) {
            const Card& card = src.cards[start_idx];
            int num_cards = (int)src.cards.size() - start_idx;

            vector<Card> seq(src.cards.begin() + start_idx, src.cards.end());
            if (!is_valid_sequence(seq)) break;

            for (const auto& dst : state.tableau) {
                if (dst.pile_type == src.pile_type) continue;

                bool can_place = false;
                if (dst.is_empty()) {
                    if (card.rank() == Rank::KING && start_idx > 0)
                        can_place = true;
                } else {
                    const Card* top = dst.top_card();
                    if (top && !top->face_down && top->is_red() != card.is_red()
                        && top->rank() == Rank(static_cast<int>(card.rank()) + 1))
                        can_place = true;
                }

                if (!can_place) continue;

                bool exposes = start_idx > 0 && src.cards[start_idx - 1].face_down;
                bool empties = (start_idx == 0);

                int priority;
                if (exposes) {
                    priority = 700 + num_cards;
                } else if (empties) {
                    priority = 600;
                } else {
                    bool enables_foundation = false;
                    if (start_idx > 0) {
                        const Card& below = src.cards[start_idx - 1];
                        if (!below.face_down && state.foundation_accepts(below))
                            enables_foundation = true;
                    }

                    bool creates_waste_play = false;
                    if (start_idx > 0 && !state.waste.cards.empty()
                        && !state.waste.top_card()->face_down) {
                        const Card* waste_top = state.waste.top_card();
                        const Card& below = src.cards[start_idx - 1];
                        if (!below.face_down && below.is_red() != waste_top->is_red()
                            && below.rank() == Rank(static_cast<int>(waste_top->rank()) + 1))
                            creates_waste_play = true;
                    }

                    if (enables_foundation) priority = 650;
                    else if (creates_waste_play) priority = 500;
                    else continue;
                }

                moves.emplace_back(priority, Move(MoveType::TABLEAU_TO_TABLEAU,
                                                  src.pile_type, dst.pile_type, card.card_id, num_cards));
            }
        }
    }
    return moves;
}

bool PerfectSolver::is_valid_sequence(const vector<Card>& cards) const {
    for (int i = 1; i < (int)cards.size(); i++) {
        if (cards[i].face_down) return false;
        if (cards[i].is_red() == cards[i-1].is_red()) return false;
        if (cards[i].rank() != Rank(static_cast<int>(cards[i-1].rank()) - 1)) return false;
    }
    return true;
}

vector<Move> PerfectSolver::remove_cycles(const GameState& initial, vector<Move> moves) const {
    bool changed = true;
    while (changed) {
        changed = false;
        GameState state = initial.clone();
        unordered_map<int64_t, int> seen;
        seen[state.state_hash()] = 0;

        vector<Move> result;
        for (int i = 0; i < (int)moves.size(); i++) {
            state.apply_move(moves[i]);
            result.push_back(moves[i]);
            int64_t h = state.state_hash();

            if (seen.count(h)) {
                int cycle_start = seen[h];
                result = vector<Move>(result.begin(), result.begin() + cycle_start);
                state = initial.clone();
                for (const Move& m : result) state.apply_move(m);
                seen.clear();
                seen[initial.state_hash()] = 0;
                GameState tmp = initial.clone();
                for (int j = 0; j < (int)result.size(); j++) {
                    tmp.apply_move(result[j]);
                    seen[tmp.state_hash()] = j + 1;
                }
                changed = true;
            } else {
                seen[h] = (int)result.size();
            }
        }
        moves = std::move(result);
    }
    return moves;
}
