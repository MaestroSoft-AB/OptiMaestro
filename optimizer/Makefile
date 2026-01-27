# =================================================
# ============== Optimizer Makefile ===============
# =================================================

# Compiler 
CC := gcc

BUILD_DIR := build
BIN_DIR		:= ../bin

# ----- Local server code -----
SRC_DIR := src
SRC := $(shell find -L $(SRC_DIR) -type f -name '*.c')

# ----- MaestroCore (submodule) -----
MAESTRO_DIR ?= ../external/MaestroCore
MAESTRO_LIB := $(MAESTRO_DIR)/build/lib/libmaestrocore.a

# Enable MaestroCore JSON (optional): make JSON=1
JSON ?= 1

# Temp dirs
CACHE_DIR := data/cache

# Compilation flags
CFLAGS := -std=c99 -MMD -MP -Wall -Wextra -Wfatal-errors -g \
	-Iinclude \
	-I$(MAESTRO_DIR)/modules/include \
	-I$(MAESTRO_DIR)/utils/include

ifeq ($(JSON),1)
	CFLAGS += -DMAESTROUTILS_WITH_CJSON
endif

ASAN_FLAGS := -O1 -g3 -fno-omit-frame-pointer -fsanitize=address,undefined

# Linker flags (auto-detect platform)
LDFLAGS := -flto -Wl,--gc-sections

# Linked libraries
LIBS := -lcurl

# Map .c → .o
SRC_OBJ   := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/$(SRC_DIR)/%.o,$(SRC))
CORE_OBJ  := $(patsubst ../$(CORE_SRC_DIR)/%.c,$(BUILD_DIR)/$(CORE_SRC_DIR)/%.o,$(CORE_SRC))
UTILS_OBJ := $(patsubst ../$(UTILS_SRC_DIR)/%.c,$(BUILD_DIR)/$(UTILS_SRC_DIR)/%.o,$(UTILS_SRC))
LIBS_OBJ  := $(patsubst ../$(LIBS_SRC_DIR)/%.c,$(BUILD_DIR)/$(LIBS_SRC_DIR)/%.o,$(LIBS_SRC))

OBJ := $(SRC_OBJ) $(CORE_OBJ) $(UTILS_OBJ) $(LIBS_OBJ)

# Dependency files (.d)
DEP := $(OBJ:.o=.d)

# Binary name
BIN := optimizer 

# ==================== Recipes ====================

all: $(BIN)
	@echo "Build complete."

$(BIN): $(OBJ)
	@echo "Linking..."
	@mkdir -p $(BIN_DIR)
	@$(CC) $(LDFLAGS) $(OBJ) -o $(BIN_DIR)/$@ $(LIBS)

asan: clean
	@echo "Building ASan binary..."
	@$(MAKE) CC=$(CC) CFLAGS="$(CFLAGS) $(ASAN_FLAGS)" LDFLAGS="$(LDFLAGS) $(ASAN_FLAGS)" all

$(BUILD_DIR)/$(CORE_SRC_DIR)/%.o: ../$(CORE_SRC_DIR)/%.c
	@echo "Compiling $<..."
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/$(UTILS_SRC_DIR)/%.o: ../$(UTILS_SRC_DIR)/%.c
	@echo "Compiling $<..."
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/$(LIBS_SRC_DIR)/%.o: ../$(LIBS_SRC_DIR)/%.c
	@echo "Compiling $<..."
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/$(SRC_DIR)/%.o: $(SRC_DIR)/%.c
	@echo "Compiling $<..."
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c $< -o $@

run: $(BIN)
	@echo "Running $<..."
	@./$(BIN)

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
	@valgrind -s --leak-check=full --show-leak-kinds=all --track-origins=yes ./$(BIN)

gdb: $(BIN)
	@echo "Debugging $< using gdb..."
	@gdb ./$(BIN)


run-asan: asan
	@echo "Running ASan build..."
	@./$(BIN)

asan:
	@$(MAKE) CC=$(CC) CFLAGS="$(CFLAGS) $(ASAN_FLAGS)" LDFLAGS="$(LDFLAGS) $(ASAN_FLAGS)" all

print:
	@echo "Source files: $(SRC)"
	@echo "Object files: $(OBJ)"
	@echo "Dependency files: $(DEP)"

-include $(DEP)

# ==================== Phonies ====================

.PHONY: all run clean print gdb valgrind
