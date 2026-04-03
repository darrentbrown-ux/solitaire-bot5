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
    bool solved;
    std::vector<Move> moves;
    int nodes_explored = 0;
    double elapsed = 0.0;
    std::string reason;

    SolveResult() : solved(false), nodes_explored(0), elapsed(0.0) {}
    SolveResult(bool ok, std::vector<Move>&& m, int nodes, double time_s)
        : solved(ok), moves(std::move(m)), nodes_explored(nodes), elapsed(time_s) {}
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

    std::vector<Move> move_queue_;
    int current_index_ = 0;
    bool solved_ = false;

    // Internal DFS state
    int nodes_explored_ = 0;
    double start_time_ = 0.0;
    std::unordered_map<int64_t, int> visited_;  // state_hash -> best foundation count

    bool timed_out() const;
    int foundation_count(const GameState& state) const;
    std::vector<Move> dfs(GameState state, int depth, int stock_passes,
                          const std::vector<Move>* recent_tab_moves);

    std::vector<Move> generate_ordered_moves(const GameState& state, int stock_passes);
    std::vector<std::pair<int, Move>> tableau_to_tableau_moves(const GameState& state);
    std::vector<Move> apply_forced_moves(GameState& state);
    bool is_auto_foundation_card(const Card& card, const GameState& state) const;
    bool is_reverse_of_recent(const Move& move, const std::vector<Move>& recent_tab) const;
    bool is_valid_sequence(const std::vector<Card>& cards) const;

    std::vector<Move> remove_cycles(const GameState& initial, std::vector<Move> moves) const;
};

#endif // SOLVER_HPP
