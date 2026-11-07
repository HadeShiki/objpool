CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic -Iinclude
LDFLAGS  ?=

BUILD_DIR := build

ifeq ($(OS),Windows_NT)
    SHELL    := cmd.exe
    EXE      := .exe
    MKDIR    := if not exist "$(BUILD_DIR)" mkdir "$(BUILD_DIR)"
    RMDIR    := if exist "$(BUILD_DIR)" rmdir /s /q "$(BUILD_DIR)" 2>nul
    RM_F     := del /q 2>nul
    RUN_ALL = $(foreach e,$(EXAMPLES),echo === $(e) === & $(BUILD_DIR)\$(e)$(EXE) & $(ECHO_BLANK))
else
    UNAME_S := $(shell uname -s)

    EXE      :=
    MKDIR    := mkdir -p "$(BUILD_DIR)"
    RMDIR    := rm -rf "$(BUILD_DIR)"
    RM_F     := rm -f
    RUN_ALL  = for e in $(EXAMPLES); do echo "=== $$e ==="; ./$(BUILD_DIR)/$$e; echo; done

    ifeq ($(UNAME_S),Darwin)
        LDFLAGS += -Wl,-dead_strip
    endif

    ifeq ($(UNAME_S),Linux)
        LDFLAGS += -Wl,--gc-sections
    endif
endif

TEST_SRC  := tests/test_pool.cpp
TEST_BIN  := $(BUILD_DIR)/objpool_test$(EXE)

EXAMPLES  := demo
EX_BINS   := $(addprefix $(BUILD_DIR)/,$(addsuffix $(EXE),$(EXAMPLES)))
EX_SRCS   := $(addprefix src/,$(addsuffix .cpp,$(EXAMPLES)))

.PHONY: all test examples run clean help

all: $(TEST_BIN) $(EX_BINS)

$(TEST_BIN): $(TEST_SRC) include/objpool.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(TEST_SRC) -o $@ $(LDFLAGS)

test: $(TEST_BIN)
	@echo "=== Running tests ==="
	@$(TEST_BIN)

$(BUILD_DIR)/%$(EXE): src/%.cpp include/objpool.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

examples: $(EX_BINS)

run: $(EX_BINS)
	@echo "=== Running all examples ==="
	@$(RUN_ALL)

$(BUILD_DIR):
	@$(MKDIR)

clean:
	@echo "=== Cleaning build directory ==="
	@$(RMDIR)

help:
	@echo "Usage: make [target]"
	@echo ""
	@echo "Targets:"
	@echo "  all       Build test and all examples (default)"
	@echo "  test      Build and run test"
	@echo "  examples  Build all examples"
	@echo "  run       Build and run all examples"
	@echo "  clean     Remove build directory"
	@echo "  help      Show this help message"
	@echo ""
	@echo "Variables:"
	@echo "  CXX       Compiler (default: g++)"
	@echo "  CXXFLAGS  Compiler flags (default: -std=c++17 -O2 -Wall -Wextra -Wpedantic -Iinclude)"
	@echo "  LDFLAGS   Linker flags"