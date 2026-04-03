#include "game_state.hpp"
#include "solver.hpp"
#include "memory_reader.hpp"
#include "input_controller.hpp"
#include <windows.h>
#include <iostream>
#include <iomanip>
#include <csignal>
#include <cstring>
#include <cstdlib>

using namespace std;

// ============================================================================
// Defaults
// ============================================================================

static const char* DEFAULT_EXE_PATH     = R"(C:\Games\SOL_ENGLISH\sol.exe)";
static const double DEFAULT_MOVE_DELAY  = 0.2;
static const double DEFAULT_SOLVE_TIMEOUT = 30.0;  // was 5s — hard boards need more time
static const int   DEFAULT_MAX_STOCK_PASSES = 10;
static const double FAST_MOVE_DELAY      = 0.02;
static const double READ_RETRY_DELAY     = 0.5;
static const double POST_MOVE_READ_DELAY = 0.3;
static const double FAST_POST_MOVE_READ_DELAY = 0.05;

// ============================================================================
// Globals (for signal handler)
// ============================================================================

static volatile sig_atomic_t g_running = 1;

static void on_escape(int) {
    g_running = 0;
}

// ============================================================================
// Config
// ============================================================================

struct Config {
    string exe_path = DEFAULT_EXE_PATH;
    double speed = DEFAULT_MOVE_DELAY;
    double solve_timeout = DEFAULT_SOLVE_TIMEOUT;
    int max_stock_passes = DEFAULT_MAX_STOCK_PASSES;
    int max_attempts = 0;
    bool fast = true;           // <-- bot5 default: fast ON
    bool verbose = false;
    bool no_launch = false;
    bool exit_on_error = false;
};

// ============================================================================
// Simple argparse
// ============================================================================

void print_usage(const char* prog) {
    cerr << "Solitaire Bot 5 - Perfect-information Windows XP Solitaire player\n\n"
         << "Usage: " << prog << " [options]\n\n"
         << "Options:\n"
         << "  --exe PATH           Path to sol.exe (default: " << DEFAULT_EXE_PATH << ")\n"
         << "  --speed SECS         Delay between moves (default: " << DEFAULT_MOVE_DELAY << ")\n"
         << "  --solve-timeout SECS Max seconds solving each game (default: " << DEFAULT_SOLVE_TIMEOUT << ")\n"
         << "  --max-stock-passes N Max stock passes the solver considers (default: " << DEFAULT_MAX_STOCK_PASSES << ")\n"
         << "  --max-attempts N     Max games to attempt, 0=unlimited (default: 0)\n"
         << "  --fast               Run fast (minimal delays) (default: ON for bot5)\n"
         << "  --no-fast            Disable fast mode\n"
         << "  --verbose, -v        Show detailed output\n"
         << "  --no-launch          Don't auto-launch sol.exe\n"
         << "  --exit-on-error      Exit immediately when a move fails\n"
         << "  --help, -h           Show this help\n";
}

Config parse_args(int argc, char* argv[]) {
    Config cfg;
    for (int i = 1; i < argc; i++) {
        string arg = argv[i];
        if (arg == "--help" || arg == "-h") { print_usage(argv[0]); exit(0); }
        else if (arg == "--exe" && i + 1 < argc) cfg.exe_path = argv[++i];
        else if (arg == "--speed" && i + 1 < argc) cfg.speed = atof(argv[++i]);
        else if (arg == "--solve-timeout" && i + 1 < argc) cfg.solve_timeout = atof(argv[++i]);
        else if (arg == "--max-stock-passes" && i + 1 < argc) cfg.max_stock_passes = atoi(argv[++i]);
        else if (arg == "--max-attempts" && i + 1 < argc) cfg.max_attempts = atoi(argv[++i]);
        else if (arg == "--fast") cfg.fast = true;
        else if (arg == "--no-fast") cfg.fast = false;
        else if (arg == "--verbose" || arg == "-v") cfg.verbose = true;
        else if (arg == "--no-launch") cfg.no_launch = true;
        else if (arg == "--exit-on-error") cfg.exit_on_error = true;
        else { cerr << "Unknown option: " << arg << "\n"; print_usage(argv[0]); exit(1); }
    }
    return cfg;
}

// ============================================================================
// Utilities
// ============================================================================

static string card_full_name(const Card* c) {
    if (!c) return "nothing";
    static const char* RN[] = {"ace","two","three","four","five","six","seven","eight",
                               "nine","ten","jack","queen","king"};
    static const char* SN[] = {"clubs","diamonds","hearts","spades"};
    Rank r = c->rank(); Suit s = c->suit();
    return string(RN[static_cast<int>(r)]) + " of " + SN[static_cast<int>(s)];
}


static const Card* lookup_move_card(const Move& move, const GameState& state) {
    if (move.card_id < 0) return nullptr;
    if (move.source == PileType::WASTE && !state.waste.is_empty())
        return state.waste.top_card();
    if (move.source >= PileType::TABLEAU_0 && move.source <= PileType::TABLEAU_6) {
        int idx = static_cast<int>(move.source) - static_cast<int>(PileType::TABLEAU_0);
        if (!state.tableau[idx].is_empty())
            return state.tableau[idx].top_card();
    }
    return nullptr;
}

static string pile_summary(const Pile& pile) {
    if (pile.is_empty()) return "empty";
    vector<string> parts;
    for (const auto& c : pile.cards) {
        if (!c.face_down) parts.push_back(c.display_str());
    }
    if (parts.empty()) return to_string(pile.face_down_count()) + " hidden cards";
    string s;
    for (auto& x : parts) s += x + " ";
    if (pile.face_down_count()) s = to_string(pile.face_down_count()) + " hidden + " + s;
    return s;
}

static string describe_move_attempt(const Move& move, const GameState& state) {
    const Card* card = lookup_move_card(move, state);
    if (!card) return move.to_string();

    if (move.move_type == MoveType::WASTE_TO_TABLEAU) {
        int dst_idx = static_cast<int>(move.dest) - static_cast<int>(PileType::TABLEAU_0);
        const Pile& dst = state.tableau[dst_idx];
        const Card* actual_waste = state.waste.top_card();
        return "Attempted to move " + card_full_name(card) + " from waste to tableau " +
               to_string(dst_idx) + " which contains " + pile_summary(dst) +
               ". Actual waste top: " + card_full_name(actual_waste) + ".";
    }
    if (move.move_type == MoveType::TABLEAU_TO_TABLEAU) {
        int src_idx = static_cast<int>(move.source) - static_cast<int>(PileType::TABLEAU_0);
        int dst_idx = static_cast<int>(move.dest) - static_cast<int>(PileType::TABLEAU_0);
        const Pile& src = state.tableau[src_idx];
        const Pile& dst = state.tableau[dst_idx];
        return "Attempted to move " + card_full_name(card) + " (" + to_string(move.num_cards) +
               " cards) from tableau " + to_string(src_idx) + " (" + pile_summary(src) +
               ") to tableau " + to_string(dst_idx) + " (" + pile_summary(dst) + ").";
    }
    if (move.move_type == MoveType::WASTE_TO_FOUNDATION) {
        const Card* actual_waste = state.waste.top_card();
        return "Attempted to move " + card_full_name(card) + " from waste to foundation. "
               "Actual waste top: " + card_full_name(actual_waste) + ".";
    }
    if (move.move_type == MoveType::TABLEAU_TO_FOUNDATION) {
        int src_idx = static_cast<int>(move.source) - static_cast<int>(PileType::TABLEAU_0);
        const Pile& src = state.tableau[src_idx];
        return "Attempted to move " + card_full_name(card) + " from tableau " +
               to_string(src_idx) + " to foundation. Actual top: " +
               card_full_name(src.top_card()) + ".";
    }
    return move.to_string();
}

static int64_t hash_state(const GameState& state) {
    return state.state_hash();
}

// ============================================================================
// Bot
// ============================================================================

struct Bot {
    Config& cfg;
    MemoryReader* reader = nullptr;
    InputController* controller = nullptr;
    PerfectSolver* solver = nullptr;

    int games_attempted = 0, games_solved = 0, games_won = 0, games_unsolvable = 0;
    int total_moves = 0;
    double total_solve_time = 0.0;
    int64_t total_nodes = 0;

    explicit Bot(Config& c) : cfg(c) {}

    void log(const string& msg) { cout << "[Bot] " << msg << "\n"; }
    void vlog(const string& msg) { if (cfg.verbose) cout << "  > " << msg << "\n"; }

    void start() {
        log("Solitaire Bot 5 - Perfect Information Solver (C++)");
        log("Solve timeout: " + to_string(cfg.solve_timeout) + "s | "
            "Fast: " + string(cfg.fast ? "yes" : "no") + " | "
            "Max attempts: " + (cfg.max_attempts ? to_string(cfg.max_attempts) : "unlimited"));
        log("Defaults: --solve-timeout 30 --fast  (override with --solve-timeout N --no-fast)\n");
        log("Press Escape to stop.\n");

        signal(SIGINT, on_escape);

        ensure_solitaire_running();
        connect();

        while (g_running) {
            if (cfg.max_attempts > 0 && games_attempted >= cfg.max_attempts) {
                log("Reached max attempts (" + to_string(cfg.max_attempts) + "). Stopping.");
                break;
            }
            try {
                string result = solve_and_play();
                if (!g_running) break;
                if (result == "won") {
                    log("Waiting for win animation...");
                    controller->accept_deal_again();
                } else {
                    log("Dealing new game...");
                    controller->new_game();
                }
            } catch (const GameNotStartedError& e) {
                vlog(string("No active game: ") + e.what() + " waiting...");
                Sleep(1000);
            } catch (const MemoryReadError& e) {
                vlog(string("Memory read error: ") + e.what());
                if (!reconnect()) { log("Lost connection to sol.exe."); break; }
                Sleep(1000);
            } catch (const exception& e) {
                log(string("Error: ") + e.what());
                if (cfg.exit_on_error) { log("--exit-on-error set. Exiting."); break; }
                Sleep(1000);
            }
        }

        print_stats();
        if (reader) { reader->close(); delete reader; }
        delete controller;
        delete solver;
    }

    void ensure_solitaire_running() {
        DWORD pid = find_process_id("sol.exe");
        if (pid) {
            log(string("Found running sol.exe (PID: ") + to_string(pid) + ")");
        } else if (cfg.no_launch) {
            throw ProcessNotFoundError("sol.exe is not running and --no-launch was specified.");
        } else {
            log(string("Launching Solitaire from: ") + cfg.exe_path);
            pid = launch_solitaire(cfg.exe_path.c_str());
            if (!pid) throw runtime_error("Failed to launch sol.exe");
            log(string("Launched sol.exe (PID: ") + to_string(pid) + ")");
        }
    }

    void connect() {
        DWORD pid = find_process_id("sol.exe");
        if (!pid) throw ProcessNotFoundError("sol.exe disappeared!");

        reader = new MemoryReader(pid);
        double move_delay = cfg.fast ? FAST_MOVE_DELAY : cfg.speed;
        controller = new InputController(move_delay, cfg.fast);
        solver = new PerfectSolver(cfg.solve_timeout, cfg.max_stock_passes, cfg.verbose);
        log("Connected to Solitaire.");
    }

    bool reconnect() {
        try {
            if (reader) reader->close();
            DWORD pid = find_process_id("sol.exe");
            if (!pid) return false;
            reader = new MemoryReader(pid);
            controller = new InputController(cfg.fast ? FAST_MOVE_DELAY : cfg.speed, cfg.fast);
            return true;
        } catch (...) { return false; }
    }

    string solve_and_play() {
        games_attempted++;
        log("--- Game #" + to_string(games_attempted) + " ---");

        GameState state = read_state();
        if (cfg.verbose) {
            cout << state.display();
            display_hidden_cards(state);
        }

        log("Solving...");
        SolveResult result = solver->solve(state);
        total_solve_time += result.elapsed;
        total_nodes += result.nodes_explored;

        if (!result.solved) {
            games_unsolvable++;
            log("X Unsolvable: " + result.reason + " (" +
                to_string(result.nodes_explored) + " nodes in " +
                to_string_fixed(result.elapsed, 2) + "s)");
            return "unsolvable";
        }

        games_solved++;
        log("OK Solution found: " + to_string((int)result.moves.size()) + " moves (" +
            to_string(result.nodes_explored) + " nodes in " +
            to_string_fixed(result.elapsed, 2) + "s)");

        return execute_solution(result);
    }

    string execute_solution(const SolveResult& result) {
        // won=true: initial state was already solved (0 moves)
        if (result.won) {
            games_won++;
            log(":D Game WON! (0 moves executed — pre-auto-clear)");
            return "won";
        }

        const vector<Move>& moves = result.moves;
        int move_count = 0;
        const int MAX_RETRIES = 3;
        GameState state = read_state();
        int64_t old_hash = hash_state(state);

        // Empty non-won path (shouldn't happen, but guard against it)
        if (moves.empty()) {
            log("! Solution is empty (internal error)");
            return "failed";
        }

        for (size_t i = 0; i < moves.size() && g_running; ) {
            const Move& move = moves[i];
            // Skip sentinel (found-won marker)
            if (move.is_no_move()) { ++i; continue; }

            state = read_state();
            if (state.is_won()) {
                games_won++;
                log(":D Game WON! (" + to_string(move_count) + " moves executed)");
                return "won";
            }

            vlog("[" + to_string(move_count + 1) + "/" + to_string((int)moves.size()) +
                 "] " + move.to_string());

            string error_detail = describe_move_attempt(move, state);

            // Flip exposed face-down cards before the move
            flip_exposed_cards(state);
            Sleep(50);
            state = read_state();
            old_hash = hash_state(state);

            bool success = false;
            for (int attempt = 0; attempt < MAX_RETRIES; attempt++) {
                controller->execute_move(move, state);
                double post_delay = cfg.fast ? FAST_POST_MOVE_READ_DELAY : POST_MOVE_READ_DELAY;
                Sleep((DWORD)(post_delay * 1000));

                GameState new_state = read_state();
                if (hash_state(new_state) != old_hash) {
                    success = true;
                    state = std::move(new_state);
                    break;
                } else if (attempt < MAX_RETRIES - 1) {
                    vlog("! Move had no effect, retrying... (attempt " +
                         to_string(attempt + 2) + "/" + to_string(MAX_RETRIES) + ")");
                    state = read_state();
                    Sleep(100);
                }
            }

            if (!success) {
                log("! Move failed after " + to_string(MAX_RETRIES) + " attempts at step " +
                    to_string(move_count + 1) + ". " + error_detail);
                if (cfg.exit_on_error) {
                    log("--exit-on-error is set. Exiting.");
                    g_running = 0;
                    return "failed";
                }
                log("Aborting solution.");
                return "failed";
            }

            move_count++;
            total_moves++;
            ++i;
            flip_exposed_cards(state);
            Sleep((DWORD)(cfg.fast ? 30 : 100));
        }

        // All moves executed — check final state
        state = read_state();
        if (state.is_won()) {
            games_won++;
            log(":D Game WON! (" + to_string(move_count) + " moves executed)");
            return "won";
        } else {
            log("! Solution executed but game not won (" +
                to_string(move_count) + " moves). Possible execution error.");
            return "failed";
        }
    }

    void flip_exposed_cards(GameState& state) {
        for (auto& t : state.tableau) {
            if (t.is_empty()) continue;
            if (t.top_card() && t.top_card()->face_down) {
                vlog("Flipping exposed card on Tableau " + to_string(t.tableau_index()));
                controller->flip_top_card(t);
                Sleep((DWORD)(cfg.fast ? 30 : 200));
            }
        }
    }

    GameState read_state() {
        for (int attempt = 0; attempt < 3; attempt++) {
            try {
                GameState s = reader->read_game_state();
                if (s.total_cards() == 52) return s;
                vlog("Warning: " + to_string(s.total_cards()) +
                     " cards detected (expected 52), retrying...");
                Sleep((DWORD)(READ_RETRY_DELAY * 1000));
            } catch (...) {
                if (attempt < 2) Sleep((DWORD)(READ_RETRY_DELAY * 1000));
            }
        }
        return reader->read_game_state();
    }

    void display_hidden_cards(const GameState& state) {
        cout << "  Hidden cards:\n";
        for (const auto& t : state.tableau) {
            vector<string> hidden;
            for (const auto& c : t.cards) if (c.face_down) hidden.push_back(c.display_str());
            if (!hidden.empty()) {
                string s;
                for (auto& x : hidden) s += x + " ";
                cout << "    Tableau " << t.tableau_index() << ": " << s << "\n";
            }
        }
        if (!state.stock.cards.empty()) {
            string s;
            for (const auto& c : state.stock.cards) s += c.display_str() + " ";
            cout << "    Stock: " << s << "\n";
        }
        cout << "\n";
    }

    void print_stats() {
        log("");
        log("=== Session Stats ===");
        log("Games attempted:  " + to_string(games_attempted));
        log("Games solved:     " + to_string(games_solved));
        log("Games won:        " + to_string(games_won));
        log("Games unsolvable: " + to_string(games_unsolvable));
        if (games_attempted > 0) {
            double solve_rate = (double)games_solved / games_attempted * 100.0;
            double win_rate = (double)games_won / games_attempted * 100.0;
            log("Solve rate:       " + to_string_fixed(solve_rate, 1) + "%");
            log("Win rate:         " + to_string_fixed(win_rate, 1) + "%");
        }
        log("Total moves:      " + to_string(total_moves));
        log("Total solve time: " + to_string_fixed(total_solve_time, 2) + "s");
        log("Total nodes:      " + to_string(total_nodes));
        if (games_solved > 0) {
            log("Avg solve time:   " + to_string_fixed(total_solve_time / games_solved, 2) + "s");
            log("Avg nodes/solve:  " + to_string((int64_t)(total_nodes / games_solved)));
        }
    }

private:
    static string to_string(int v) { ostringstream s; s << v; return s.str(); }
    static string to_string(unsigned long v) { ostringstream s; s << v; return s.str(); }
    static string to_string(double v) { ostringstream s; s << v; return s.str(); }
    static string to_string(int64_t v) { ostringstream s; s << v; return s.str(); }
    static string to_string_fixed(double v, int decimals) {
        ostringstream s; s << fixed << setprecision(decimals) << v; return s.str();
    }
};

// ============================================================================
// Entry Point
// ============================================================================

int main(int argc, char* argv[]) {
    // Check Windows
#ifndef _WIN32
    cerr << "Error: This bot only runs on Windows.\n";
    return 1;
#endif

    Config cfg = parse_args(argc, argv);

    try {
        Bot bot(cfg);
        bot.start();
    } catch (const ProcessNotFoundError& e) {
        cerr << "[Bot] Error: " << e.what() << "\n";
        return 1;
    } catch (const exception& e) {
        cerr << "[Bot] Fatal error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
