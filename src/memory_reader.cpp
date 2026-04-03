#include "memory_reader.hpp"
#include <algorithm>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#else
#error "memory_reader.cpp requires Windows"
#endif

using namespace std;

// ============================================================================
// Constants (reverse-engineered from sol.exe)
// ============================================================================

static const DWORD MEM_PROCESS_VM_READ = 0x0010;
static const DWORD MEM_PROCESS_QUERY_INFO = 0x0400;
static const DWORD MEM_GAME_OBJECT_PTR_ADDR = 0x01007170;
static const DWORD MEM_DRAW_COUNT_ADDR     = 0x0100702C;
static const DWORD MEM_GAME_NUMBER_ADDR     = 0x01007344;
static const DWORD MEM_PILE_COUNT_OFFSET    = 0x64;
static const DWORD MEM_PILE_ARRAY_OFFSET     = 0x6C;
static const DWORD MEM_PILE_X_OFFSET        = 0x08;
static const DWORD MEM_PILE_Y_OFFSET        = 0x0C;
static const DWORD MEM_PILE_CARD_COUNT_OFF  = 0x1C;
static const DWORD MEM_PILE_CARD_ARRAY_OFF  = 0x24;
static const DWORD MEM_CARD_SIZE            = 12;
static const INT   MEM_EXPECTED_PILE_COUNT  = 13;
static const INT   MEM_MAX_CARDS_PER_PILE   = 52;

// ============================================================================
// Process enumeration
// ============================================================================

DWORD find_process_id(const char* process_name) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32 entry;
    entry.dwSize = sizeof(PROCESSENTRY32);

    if (!Process32First(snapshot, &entry)) {
        CloseHandle(snapshot);
        return 0;
    }

    DWORD pid = 0;
    do {
        if (_stricmp(entry.szExeFile, process_name) == 0) {
            pid = entry.th32ProcessID;
            break;
        }
    } while (Process32Next(snapshot, &entry));

    CloseHandle(snapshot);
    return pid;
}

DWORD launch_solitaire(const char* exe_path) {
    SHELLEXECUTEINFO info = {0};
    info.cbSize = sizeof(SHELLEXECUTEINFO);
    info.fMask = SEE_MASK_NOCLOSEPROCESS;
    info.lpVerb = "open";
    info.lpFile = exe_path;
    info.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteEx(&info)) return 0;
    if (!info.hProcess) return 0;

    Sleep(2000);
    DWORD pid = find_process_id("sol.exe");
    CloseHandle(info.hProcess);
    return pid;
}

// ============================================================================
// MemoryReader
// ============================================================================

MemoryReader::MemoryReader(DWORD pid) : pid_(pid) {
    open_process();
}

MemoryReader::~MemoryReader() {
    close();
}

void MemoryReader::open_process() {
    process_handle_ = OpenProcess(MEM_PROCESS_VM_READ | MEM_PROCESS_QUERY_INFO, FALSE, pid_);
    if (!process_handle_) {
        DWORD err = GetLastError();
        char buf[256];
        snprintf(buf, sizeof(buf), "Cannot open process %lu (error %lu). "
                 "Try running as Administrator.", pid_, err);
        throw MemoryReadError(buf);
    }
}

void MemoryReader::close() {
    if (process_handle_) {
        CloseHandle(process_handle_);
        process_handle_ = nullptr;
    }
}

void* MemoryReader::read_bytes(LPVOID address, SIZE_T size) const {
    static vector<char> buffer;
    buffer.resize(size);
    SIZE_T bytes_read = 0;
    BOOL ok = ReadProcessMemory(process_handle_, address, buffer.data(), size, &bytes_read);
    if (!ok || bytes_read != size) {
        char buf[256];
        snprintf(buf, sizeof(buf), "Failed to read %zu bytes at %p", size, address);
        throw MemoryReadError(buf);
    }
    return buffer.data();
}

DWORD MemoryReader::read_dword(LPVOID address) const {
    const void* data = read_bytes(address, 4);
    return *static_cast<const DWORD*>(data);
}

WORD MemoryReader::read_word(LPVOID address) const {
    const void* data = read_bytes(address, 2);
    return *static_cast<const WORD*>(data);
}

LONG MemoryReader::read_signed_dword(LPVOID address) const {
    const void* data = read_bytes(address, 4);
    return *static_cast<const LONG*>(data);
}

Card MemoryReader::read_card(LPVOID address) const {
    const char* data = static_cast<const char*>(read_bytes(address, MEM_CARD_SIZE));
    WORD word = *reinterpret_cast<const WORD*>(data);
    int x = *reinterpret_cast<const LONG*>(data + 4);
    int y = *reinterpret_cast<const LONG*>(data + 8);
    return Card::from_memory(word, x, y);
}

Pile MemoryReader::read_pile(LPVOID pile_ptr, PileType pile_type) const {
    Pile pile(pile_type);
    if (!pile_ptr) return pile;

    pile.x = read_signed_dword(static_cast<char*>(pile_ptr) + MEM_PILE_X_OFFSET);
    pile.y = read_signed_dword(static_cast<char*>(pile_ptr) + MEM_PILE_Y_OFFSET);
    DWORD card_count = read_dword(static_cast<char*>(pile_ptr) + MEM_PILE_CARD_COUNT_OFF);
    if ((int)card_count > MEM_MAX_CARDS_PER_PILE) card_count = 0;

    pile.cards.reserve(card_count);
    char* card_array_start = static_cast<char*>(pile_ptr) + MEM_PILE_CARD_ARRAY_OFF;
    for (DWORD j = 0; j < card_count; j++) {
        pile.cards.push_back(read_card(card_array_start + j * MEM_CARD_SIZE));
    }
    return pile;
}

LPVOID MemoryReader::read_game_object_ptr() const {
    return reinterpret_cast<LPVOID>(read_dword(reinterpret_cast<LPVOID>(MEM_GAME_OBJECT_PTR_ADDR)));
}

GameState MemoryReader::read_game_state() {
    LPVOID game_ptr = read_game_object_ptr();
    if (!game_ptr) {
        throw GameNotStartedError(
            "No active game (game object pointer is null). "
            "Start a new game in Solitaire first.");
    }

    DWORD pile_count = read_dword(static_cast<char*>(game_ptr) + MEM_PILE_COUNT_OFFSET);
    if ((int)pile_count != MEM_EXPECTED_PILE_COUNT) {
        char buf[256];
        snprintf(buf, sizeof(buf),
                 "Unexpected pile count: %lu (expected %d). The game may not be initialized yet.",
                 pile_count, MEM_EXPECTED_PILE_COUNT);
        throw GameNotStartedError(buf);
    }

    DWORD draw_count = read_dword(reinterpret_cast<LPVOID>(MEM_DRAW_COUNT_ADDR));
    if (draw_count != 1 && draw_count != 3) draw_count = 1;

    // Read all pile pointers
    vector<LPVOID> pile_ptrs;
    for (int i = 0; i < MEM_EXPECTED_PILE_COUNT; i++) {
        LPVOID ptr = reinterpret_cast<LPVOID>(
            read_dword(static_cast<char*>(game_ptr) + MEM_PILE_ARRAY_OFFSET + i * 4));
        pile_ptrs.push_back(ptr);
    }

    // Read each pile
    vector<Pile> piles;
    for (int i = 0; i < MEM_EXPECTED_PILE_COUNT; i++) {
        piles.push_back(read_pile(pile_ptrs[i], PileType(i)));
    }

    GameState state;
    state.stock = std::move(piles[0]);
    state.waste = std::move(piles[1]);
    state.foundations.clear();
    for (int i = 0; i < 4; i++) state.foundations.push_back(std::move(piles[2 + i]));
    state.tableau.clear();
    for (int i = 0; i < 7; i++) state.tableau.push_back(std::move(piles[6 + i]));
    state.draw_count = static_cast<int>(draw_count);

    return state;
}

DWORD MemoryReader::read_game_number() const {
    return read_dword(reinterpret_cast<LPVOID>(MEM_GAME_NUMBER_ADDR));
}

bool MemoryReader::is_process_alive() const {
    if (!process_handle_) return false;
    DWORD exit_code = 0;
    BOOL ok = GetExitCodeProcess(process_handle_, &exit_code);
    return ok && exit_code == STILL_ACTIVE;
}
