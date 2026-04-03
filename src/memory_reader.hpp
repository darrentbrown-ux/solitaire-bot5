#pragma once
#ifndef MEMORY_READER_HPP
#define MEMORY_READER_HPP

#include "game_state.hpp"
#include <string>
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#else
#error "memory_reader.hpp requires Windows"
#endif

// ============================================================================
// Errors
// ============================================================================

struct MemoryReadError : public std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct ProcessNotFoundError : public std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct GameNotStartedError : public std::runtime_error {
    using std::runtime_error::runtime_error;
};

// ============================================================================
// MemoryReader
// ============================================================================

class MemoryReader {
public:
    explicit MemoryReader(DWORD pid);
    ~MemoryReader();

    GameState read_game_state();
    DWORD read_game_number() const;
    bool is_process_alive() const;
    void close();

private:
    DWORD pid_;
    HANDLE process_handle_ = nullptr;

    void open_process();
    void* read_bytes(LPVOID address, SIZE_T size) const;
    DWORD read_dword(LPVOID address) const;
    WORD read_word(LPVOID address) const;
    LONG read_signed_dword(LPVOID address) const;
    Card read_card(LPVOID card_addr) const;
    Pile read_pile(LPVOID pile_ptr, PileType pile_type) const;
    LPVOID read_game_object_ptr() const;
};

DWORD find_process_id(const char* process_name = "sol.exe");
DWORD launch_solitaire(const char* exe_path);

#endif // MEMORY_READER_HPP
