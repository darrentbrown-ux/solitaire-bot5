# Solitaire Bot 5 - Makefile
# Requires: Visual Studio (MSVC) or MinGW-w64 with g++

# ---------------------------------------------------------------------------
# Choose your compiler:
#   MSVC (Visual Studio Developer Command Prompt):  nmake /f Makefile
#   MinGW-w64 (e.g. from MSYS2):                    mingw32-make -f Makefile
# ---------------------------------------------------------------------------

# Detect compiler
ifdef CL
  COMPILER = msvc
  CXX = cl
  OUT_FLAG = /Fe:
  INCLUDES = /I.
  CXXFLAGS = /std:c++17 /W3 /EHsc /MT /O2 /D_SECURE_SCL=0 /DNOMINMAX
  LIBS = kernel32.lib user32.lib winmm.lib psapi.lib shell32.lib
  LDFLAGS = /SUBSYSTEM:CONSOLE
else
  COMPILER = gnu
  CXX = g++
  OUT_FLAG = -o 
  INCLUDES = -I.
  CXXFLAGS = -std=c++17 -Wall -O2 -static -static-libgcc -static-libstdc++
  LIBS = -lkernel32 -luser32 -lwinmm -lpsapi -lshell32
  LDFLAGS = -static
endif

# ---------------------------------------------------------------------------
# Target
# ---------------------------------------------------------------------------

SRC_DIR = src
SOURCES = $(SRC_DIR)/main.cpp \
          $(SRC_DIR)/game_state.cpp \
          $(SRC_DIR)/solver.cpp \
          $(SRC_DIR)/memory_reader.cpp \
          $(SRC_DIR)/input_controller.cpp

TARGET = solitaire-bot5.exe

.PHONY: all clean

all: $(TARGET)

ifeq ($(COMPILER),msvc)
$(TARGET): $(SOURCES)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SOURCES) $(LIBS) $(OUT_FLAG)$(TARGET) $(LDFLAGS)
else
$(TARGET): $(SOURCES)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SOURCES) $(LIBS) $(LDFLAGS) $(OUT_FLAG)$(TARGET)
endif

clean:
ifeq ($(COMPILER),msvc)
	del /Q $(TARGET) 2>nul
	del /Q $(SRC_DIR)\*.obj 2>nul
else
	rm -f $(TARGET)
	rm -f $(SRC_DIR)/*.o
endif
