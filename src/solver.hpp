#pragma once
#ifndef SOLVER_HPP
#define SOLVER_HPP

#include "game_state.hpp"
#include <vector>
#include <string>
#include <unordered_map>
#include <ctime>

// ============================================================================
// SolveResult
// ============================================================================

struct SolveResult {
    bool solved = false;        // true = a winning path was found
    bool won = false;           // true = initial state was already won (0-move win)
    std::vector<Move> moves;
    int nodes_explored = 0;
    double elapsed = 0.0;
    std::string reason;

    SolveResult() : solved(false), won(false), nodes_explored(0), elapsed(0.0) {}
    SolveResult(bool ok, bool w, std::vector<Move>&& m, int nodes, double time_s)
        : solved(ok), won(w), moves(std::move(m)), nodes_explored(nodes), elapsed(time_s) {}
    SolveResult(bool ok, int nodes, double time_s, const std::string& r)
        : solved(ok), won(false), moves(), nodes_explored(nodes), elapsed(time_s), reason(r) {}
};

// ============================================================================
// PerfectSolver
// ============================================================================

class PerfectSolver {
public:
    PerfectSolver(double timeout = 5.0, int max_stock_passes = 10, bool verbose = false);

    SolveResult solve(const GameState& initial_state);

    int moves_remaining() const { return (int)move_queue_.size() - current_index_; }
    const Move* get_next_move();
    bool is_solved() const { return solved_; }

private:
    double timeout_;
    int max_stock_passes_;
    bool verbose_;

    mutable std::vector<Move> move_queue_;
    int current_index_ = 0;
    bool solved_ = false;

    // Internal DFS state
    mutable int nodes_explored_ = 0;
    mutable double start_time_ = 0.0;
    mutable std::unordered_map<int64_t, int> visited_;
    mutable bool solved_flag_ = false;

    bool timed_out() const;
    int foundation_count(const GameState& state) const;
    std::vector<Move> dfs(GameState state, int depth, int stock_passes,
                          const std::vector<Move>* recent_tab_moves,
                          int* tt_skipped = nullptr, int* won_reached = nullptr) const;

    std::vector<Move> generate_ordered_moves(const GameState& state, int stock_passes) const;
    std::vector<std::pair<int, Move>> tableau_to_tableau_moves(const GameState& state) const;
    std::vector<Move> apply_forced_moves(GameState& state) const;
    bool is_auto_foundation_card(const Card& card, const GameState& state) const;
    bool is_reverse_of_recent(const Move& move, const std::vector<Move>& recent_tab) const;
    bool is_valid_sequence(const std::vector<Card>& cards) const;

    std::vector<Move> remove_cycles(const GameState& initial, std::vector<Move> moves) const;
};

#endif // SOLVER_HPP
