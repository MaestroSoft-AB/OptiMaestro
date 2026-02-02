# =================================================
# ============== Optimizer Makefile ===============
# =================================================

# Compiler 
CC := gcc

BUILD_DIR := build
BIN_DIR		:= ../bin

# ----- Local source files -----
SRC_DIR 			 := src
COMMON_SRC_DIR := ../common/src

SRC 			 := $(shell find -L $(SRC_DIR) -type f -name '*.c')
COMMON_SRC := $(shell find -L $(COMMON_SRC_DIR) -type f -name '*.c')

# ----- MaestroCore (submodule) -----
MAESTRO_DIR ?= ../external/MaestroCore
MAESTRO_LIB := $(MAESTRO_DIR)/build/lib/libmaestrocore.a
CJSON_DIR 	:= ../external/cJSON # Needed this for local include

# Enable MaestroCore JSON (optional): make JSON=1
JSON := 1

# Temp dirs
CACHE_DIR := data/cache

# Compilation flags
CFLAGS := -std=c99 -MMD -MP -Wall -Wextra -Wfatal-errors -g \
	-Iinclude 																								\
	-I$(MAESTRO_DIR)/modules/include 													\
	-I$(MAESTRO_DIR)/utils/include														\
	-I../common/include

ifeq ($(JSON),1)
	CFLAGS += -DMAESTROUTILS_WITH_CJSON -I$(CJSON_DIR)
endif

ASAN_FLAGS := -O1 -g3 -fno-omit-frame-pointer -fsanitize=address,undefined

# Linker flags (auto-detect platform)
LDFLAGS := -flto -Wl,--gc-sections

# Linked libraries
LIBS := -lcurl

# Map .c → .o
SRC_OBJ   	:= $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/$(SRC_DIR)/%.o,$(SRC))
COMMON_OBJ  := $(patsubst $(COMMON_SRC_DIR)/%.c,$(BUILD_DIR)/$(COMMON_SRC_DIR)/%.o,$(COMMON_SRC))

OBJ := $(SRC_OBJ) $(COMMON_OBJ) 

# Dependency files (.d)
DEP := $(OBJ:.o=.d)

# Binary name
BIN := $(BIN_DIR)/optimizer 

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

# asan: clean
# 	@echo "Building ASan binary..."
# 	@$(MAKE) CC=$(CC) CFLAGS="$(CFLAGS) $(ASAN_FLAGS)" LDFLAGS="$(LDFLAGS) $(ASAN_FLAGS)" all

$(BUILD_DIR)/$(SRC_DIR)/%.o: $(SRC_DIR)/%.c
	@echo "Compiling $<..."
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c $< -o $@

run: $(BIN)
	@echo "Running $<..."
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

run-asan: asan
	@echo "Running ASan build..."
	@$(BIN)

asan:
	@$(MAKE) CC=$(CC) CFLAGS="$(CFLAGS) $(ASAN_FLAGS)" LDFLAGS="$(LDFLAGS) $(ASAN_FLAGS)" all

print:
	@echo "Source files: $(SRC)"
	@echo "Object files: $(OBJ)"
	@echo "Dependency files: $(DEP)"

-include $(DEP)

# ==================== Phonies ====================

.PHONY: all run clean print gdb valgrind
