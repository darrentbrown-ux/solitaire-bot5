# Solitaire Bot 5

A perfect-information solver for Windows XP Solitaire (Klondike), written in C++ and compiled to a single standalone `.exe` with **no prerequisites** other than `sol.exe`.

Reads ALL cards from process memory — including face-down tableau cards and the stock pile order — solves the game completely before making any moves, then executes the winning sequence.

## Differences from Bot 4 (Python)

| | Bot4 (Python) | Bot5 (C++) |
|---|---|---|
| **Language** | Python 3.8+ | C++17 (standalone .exe) |
| **Dependencies** | pywin32, keyboard | _None_ (pure Win32 API) |
| **Defaults** | `--fast` off, `--solve-timeout 60` | `--fast` on, `--solve-timeout 5` |
| **Prerequisites** | Python + pip install | Just double-click |
| **Win rate** | ~79-82% | ~79-82% |

## Target Solitaire Version

This bot targets the **Windows XP Solitaire** that ships with Windows XP/7/10/11 (the one that runs as `sol.exe`).

**sol.exe checksums:**
- MD5: `373e7a863a1a345c60edb9e20ec32311`
- SHA-256: `a6fc95a5b288593c9559bd177ec43bf9b30d8a98cf19e82bf5a1ba5600857f04`

Install at `C:\Games\SOL_ENGLISH\` or specify the path with `--exe`.

## Building from Source

### Visual Studio (MSVC)

```
cd solitaire-bot5
nmake /f Makefile
```

### MinGW-w64 (e.g. MSYS2, Git Bash)

```
cd solitaire-bot5
mingw32-make -f Makefile
```

Output: `solitaire-bot5.exe`

## Usage

```bash
# Defaults: --fast --solve-timeout 5
solitaire-bot5.exe

# Fast mode with verbose output
solitaire-bot5.exe --fast --verbose

# Longer solve timeout
solitaire-bot5.exe --solve-timeout 30

# Play exactly 10 games then stop
solitaire-bot5.exe --max-attempts 10

# Disable fast mode (slower, more visible)
solitaire-bot5.exe --no-fast
```

### All Options

| Flag | Description | Default |
|---|---|---|
| `--exe PATH` | Path to sol.exe | `C:\Games\SOL_ENGLISH\sol.exe` |
| `--speed SECS` | Delay between moves | `0.2` |
| `--solve-timeout SECS` | Max time solving each game | `30` |
| `--max-stock-passes N` | Max stock passes the solver considers | `10` |
| `--max-attempts N` | Max games (0 = unlimited) | `0` |
| `--fast` | Fast mode (minimal delays, --no-fast to disable) | **ON** |
| `--no-fast` | Disable fast mode | Off |
| `--verbose`, `-v` | Show detailed output, hidden cards, move log | Off |
| `--no-launch` | Don't auto-launch sol.exe | Off |
| `--exit-on-error` | Exit immediately when a move fails | Off |
| `--help`, `-h` | Show help | |

## How It Works

1. **Reads everything** — Reads all 52 cards from `sol.exe` process memory, including face-down cards
2. **Solves perfectly** — DFS with transposition table to find a complete winning move sequence
3. **Executes the solution** — Sends mouse/keyboard inputs to play the pre-computed sequence
4. **Redeals if unsolvable** — Detects unsolvable games instantly and moves on

Press **Escape** at any time to stop.

## Architecture

```
src/
  main.cpp              — Entry point, CLI, game loop
  game_state.hpp/cpp    — Card, Pile, GameState models
  solver.hpp/cpp        — Perfect-information DFS solver
  memory_reader.hpp/cpp — Process memory reading via Win32 API
  input_controller.hpp/cpp — Mouse/keyboard input via Win32 API
Makefile                — Build with MSVC or MinGW
```

## License

MIT
