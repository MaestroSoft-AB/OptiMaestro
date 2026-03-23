# =================================================
# ======= 🌤WeatherMaestro Server Makefile ========
# =================================================

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
	CC := clang
else
	CC := gcc
endif

BUILD_DIR := build

# Binary name
USER := $(shell whoami)
BIN_DIR	 := /home/$(USER)/.local/bin
BIN := $(BIN_DIR)/optiserver

# ----- Local server code -----
SRC_DIR := src
SRC := $(shell find -L $(SRC_DIR) -type f -name '*.c')

# ----- MaestroCore (submodule) -----
MAESTRO_DIR ?= ../external/MaestroCore
MAESTRO_LIB := $(MAESTRO_DIR)/build/lib/libmaestrocore.a

# Temp dirs
CACHE_DIR := data/cache

# Compilation flags
CFLAGS := -std=c11 -MMD -MP -Wall -Wextra -Wfatal-errors -g \
	-Iinclude \
	-I../common/include \
	-I$(MAESTRO_DIR)/modules/include \
	-I$(MAESTRO_DIR)/utils/include

CFLAGS += -DMAESTROUTILS_WITH_CJSON

ASAN_FLAGS := -O1 -g3 -fno-omit-frame-pointer -fsanitize=address,undefined

ifeq ($(UNAME_S),Darwin)
	LDFLAGS := -flto
else
	LDFLAGS := -flto -Wl,--gc-sections
endif

# Linked libraries
LIBS := -lcurl

# Map .c → .o (ONLY server sources)
SRC_OBJ := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/$(SRC_DIR)/%.o,$(SRC))
OBJ := $(SRC_OBJ)

# Dependency files (.d)
DEP := $(OBJ:.o=.d)

# ==================== Recipes ====================

all: $(BIN)
	@echo "Build complete."

# Ensure MaestroCore is built (delegates to MaestroCore Makefile)
$(MAESTRO_LIB):
	@echo "Building MaestroCore (JSON=$(JSON))..."
	@$(MAKE) -C $(MAESTRO_DIR) JSON=$(JSON)

$(BIN): $(OBJ) $(MAESTRO_LIB)
	@echo "Linking..."
	@mkdir -p $(BIN_DIR)
	@$(CC) $(LDFLAGS) $(OBJ) $(MAESTRO_LIB) -o $@ $(LIBS)

asan: clean
	@echo "Building ASan binary..."
	@$(MAKE) JSON=$(JSON) CC=$(CC) CFLAGS="$(CFLAGS) $(ASAN_FLAGS)" LDFLAGS="$(LDFLAGS) $(ASAN_FLAGS)" all

profile: clean
	@echo "Building profiling binary..."
	@$(MAKE) \
		JSON=$(JSON) \
		CC=$(CC) \
		CFLAGS="$(CFLAGS) -pg -O0 -g" \
		LDFLAGS="-Wl,--gc-sections -pg" \
		all


$(BUILD_DIR)/$(SRC_DIR)/%.o: $(SRC_DIR)/%.c
	@echo "Compiling $<..."
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c $< -o $@


run: $(BIN)
	@echo "Running $(BIN)..."
	@$(BIN)

clean:
	@echo "Cleaning build files..."
	@rm -rf $(BUILD_DIR) $(BIN)
	@echo "Clean complete."

full-clean:
	@echo "Cleaning build files..."
	@rm -rf $(BUILD_DIR) $(CACHE_DIR) $(BIN)
	@echo "Clean complete."

valgrind: $(BIN)
	@echo "Debugging $< using valgrind..."
	@valgrind -s --leak-check=full --show-leak-kinds=all --track-origins=yes $(BIN)

gdb: $(BIN)
	@echo "Debugging $< using gdb..."
	@gdb $(BIN)

print:
	@echo "Source files: $(SRC)"
	@echo "Object files: $(OBJ)"
	@echo "Dependency files: $(DEP)"
	@echo "Maestro lib: $(MAESTRO_LIB)"

-include $(DEP)

.PHONY: all run clean print gdb valgrind asan profile gprof

