#include "input_controller.hpp"
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <chrono>
#include <thread>

using namespace std;

// ============================================================================
// Helpers
// ============================================================================

static inline void sleep_ms(double ms) {
    this_thread::sleep_for(chrono::milliseconds(static_cast<DWORD>(ms)));
}

// ============================================================================
// InputController
// ============================================================================

static const DWORD MCARD_WIDTH  = 71;
static const DWORD MCARD_HEIGHT = 96;

InputController::InputController(double move_delay, bool fast)
    : move_delay_(move_delay) {
    step_delay_    = fast ? 0.01 : 0.05;
    tiny_delay_    = fast ? 0.005 : 0.02;
    drag_step_delay_ = fast ? 0.005 : 0.02;
    fg_delay_      = fast ? 0.01 : 0.05;

    hwnd_ = FindWindowW(L"Solitaire", nullptr);
    if (!hwnd_) {
        throw runtime_error("Could not find Solitaire window");
    }
}

std::pair<int,int> InputController::client_to_screen(int cx, int cy) {
    POINT pt = {cx, cy};
    ClientToScreen(hwnd_, &pt);
    return {pt.x, pt.y};
}

void InputController::ensure_foreground() {
    SetForegroundWindow(hwnd_);
    sleep_ms(fg_delay_);
}

void InputController::click_at(int cx, int cy) {
    ensure_foreground();
    auto [sx, sy] = client_to_screen(cx, cy);
    SetCursorPos(sx, sy);
    sleep_ms(step_delay_);
    mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
    sleep_ms(step_delay_);
    mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
    sleep_ms(move_delay_);
}

void InputController::double_click_at(int cx, int cy) {
    ensure_foreground();
    auto [sx, sy] = client_to_screen(cx, cy);
    SetCursorPos(sx, sy);
    sleep_ms(step_delay_);
    mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
    sleep_ms(tiny_delay_);
    mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
    sleep_ms(step_delay_);
    mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
    sleep_ms(tiny_delay_);
    mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
    sleep_ms(move_delay_);
}

void InputController::drag(int from_cx, int from_cy, int to_cx, int to_cy) {
    ensure_foreground();
    auto [fsx, fsy] = client_to_screen(from_cx, from_cy);
    auto [tsx, tsy] = client_to_screen(to_cx, to_cy);

    SetCursorPos(fsx, fsy);
    sleep_ms(step_delay_);
    mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
    sleep_ms(step_delay_);

    int steps = 5;
    for (int i = 1; i <= steps; i++) {
        int ix = fsx + (tsx - fsx) * i / steps;
        int iy = fsy + (tsy - fsy) * i / steps;
        SetCursorPos(ix, iy);
        sleep_ms(drag_step_delay_);
    }
    sleep_ms(step_delay_);
    mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
    sleep_ms(move_delay_);
}

std::pair<int,int> InputController::card_click_pos(const Pile& pile, int card_index) const {
    if (pile.is_empty())
        return {pile.x + (int)MCARD_WIDTH / 2, pile.y + (int)MCARD_HEIGHT / 2};

    if (card_index == -1) card_index = (int)pile.cards.size() - 1;
    const Card& c = pile.cards[card_index];
    bool is_last = (card_index == (int)pile.cards.size() - 1);
    int y_offset = is_last ? (int)MCARD_HEIGHT / 4 : 5;
    return {c.x + (int)MCARD_WIDTH / 2, c.y + y_offset};
}

std::pair<int,int> InputController::pile_base_pos(const Pile& pile) const {
    return {pile.x + (int)MCARD_WIDTH / 2, pile.y + (int)MCARD_HEIGHT / 2};
}

std::pair<int,int> InputController::dest_drop_pos(const Pile& pile) const {
    if (pile.is_empty()) return pile_base_pos(pile);
    return card_click_pos(pile, -1);
}

void InputController::execute_move(const Move& move, const GameState& state) {
    if (move.move_type == MoveType::NONE) return;
    switch (move.move_type) {
        case MoveType::DRAW_STOCK:
            do_draw_stock(state); break;
        case MoveType::RECYCLE_WASTE:
            do_recycle_stock(state); break;
        case MoveType::WASTE_TO_FOUNDATION:
            do_waste_to_foundation(state); break;
        case MoveType::WASTE_TO_TABLEAU:
            do_waste_to_tableau(move, state); break;
        case MoveType::TABLEAU_TO_FOUNDATION:
            do_tableau_to_foundation(move, state); break;
        case MoveType::TABLEAU_TO_TABLEAU:
            do_tableau_to_tableau(move, state); break;
    }
}

void InputController::do_draw_stock(const GameState& state) {
    auto [x, y] = pile_base_pos(state.stock);
    click_at(x, y);
}

void InputController::do_recycle_stock(const GameState& state) {
    auto [x, y] = pile_base_pos(state.stock);
    click_at(x, y);
}

void InputController::do_waste_to_foundation(const GameState& state) {
    if (!state.waste.is_empty()) {
        auto [x, y] = card_click_pos(state.waste);
        double_click_at(x, y);
    }
}

void InputController::do_waste_to_tableau(const Move& move, const GameState& state) {
    if (state.waste.is_empty()) return;
    auto [sx, sy] = card_click_pos(state.waste);
    int dst_idx = static_cast<int>(move.dest) - static_cast<int>(PileType::TABLEAU_0);
    const Pile& dst = state.tableau[dst_idx];
    auto [dx, dy] = dest_drop_pos(dst);
    drag(sx, sy, dx, dy);
}

void InputController::do_tableau_to_foundation(const Move& move, const GameState& state) {
    int src_idx = static_cast<int>(move.source) - static_cast<int>(PileType::TABLEAU_0);
    const Pile& src = state.tableau[src_idx];
    if (!src.is_empty()) {
        auto [x, y] = card_click_pos(src);
        double_click_at(x, y);
    }
}

void InputController::do_tableau_to_tableau(const Move& move, const GameState& state) {
    int src_idx = static_cast<int>(move.source) - static_cast<int>(PileType::TABLEAU_0);
    const Pile& src = state.tableau[src_idx];
    if (src.is_empty()) return;

    int card_index = (int)src.cards.size() - move.num_cards;
    auto [sx, sy] = card_click_pos(src, card_index);

    int dst_idx = static_cast<int>(move.dest) - static_cast<int>(PileType::TABLEAU_0);
    const Pile& dst = state.tableau[dst_idx];
    auto [dx, dy] = dest_drop_pos(dst);

    drag(sx, sy, dx, dy);
}

void InputController::flip_top_card(const Pile& pile) {
    if (pile.is_empty()) return;
    const Card& top = pile.cards.back();
    if (!top.face_down) return;
    auto [x, y] = card_click_pos(pile, (int)pile.cards.size() - 1);
    click_at(x, y);
}

void InputController::accept_deal_again() {
    sleep_ms(5000.0);
    ensure_foreground();
    // F2 key
    keybd_event(0x71, 0, 0, 0);
    sleep_ms(50);
    keybd_event(0x71, 0, KEYEVENTF_KEYUP, 0);
    sleep_ms(1000.0);
    // Space
    keybd_event(VK_SPACE, 0, 0, 0);
    sleep_ms(50);
    keybd_event(VK_SPACE, 0, KEYEVENTF_KEYUP, 0);
    sleep_ms(1000.0);
}

void InputController::new_game() {
    ensure_foreground();
    // F2 key
    keybd_event(0x71, 0, 0, 0);
    sleep_ms(50);
    keybd_event(0x71, 0, KEYEVENTF_KEYUP, 0);
    sleep_ms(1000.0);
}

bool InputController::is_window_alive() const {
    return IsWindow(hwnd_) != 0;
}
