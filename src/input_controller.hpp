#pragma once
#ifndef INPUT_CONTROLLER_HPP
#define INPUT_CONTROLLER_HPP

#include "game_state.hpp"
#include <windows.h>
#include <utility>

// ============================================================================
// InputController
// ============================================================================

class InputController {
public:
    InputController(double move_delay = 0.2, bool fast = false);

    void execute_move(const Move& move, const GameState& state);

    void flip_top_card(const Pile& pile);
    void accept_deal_again();
    void new_game();
    bool is_window_alive() const;

private:
    HWND hwnd_;
    double move_delay_;
    double step_delay_;
    double tiny_delay_;
    double drag_step_delay_;
    double fg_delay_;

    void ensure_foreground();
    void click_at(int cx, int cy);
    void double_click_at(int cx, int cy);
    void drag(int from_cx, int from_cy, int to_cx, int to_cy);
    std::pair<int,int> client_to_screen(int cx, int cy);

    std::pair<int,int> card_click_pos(const Pile& pile, int card_index = -1) const;
    std::pair<int,int> pile_base_pos(const Pile& pile) const;
    std::pair<int,int> dest_drop_pos(const Pile& pile) const;

    void do_draw_stock(const GameState& state);
    void do_recycle_stock(const GameState& state);
    void do_waste_to_foundation(const GameState& state);
    void do_waste_to_tableau(const Move& move, const GameState& state);
    void do_tableau_to_foundation(const Move& move, const GameState& state);
    void do_tableau_to_tableau(const Move& move, const GameState& state);
};

#endif // INPUT_CONTROLLER_HPP
