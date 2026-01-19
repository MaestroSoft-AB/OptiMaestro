# =================================================
# ============== Optimizer Makefile ===============
# =================================================

# Compiler (use clang on macOS, gcc otherwise)
UNAME_S := $(shell uname -s)
CC := gcc

BUILD_DIR := build
BIN_DIR		:= ../bin

# Source files directories
SRC_DIR   		:= src
CORE_SRC_DIR  := #core/modules/src
UTILS_SRC_DIR := #core/utils/src
LIBS_SRC_DIR  := #libs/src

# Source files to build
SRC       := $(shell find -L $(SRC_DIR) 	-type f -name '*.c')
CORE_SRC  := $(shell find -L ../$(CORE_SRC_DIR)  -type f -name '*.c')
UTILS_SRC := $(shell find -L ../$(UTILS_SRC_DIR) -type f -name '*.c')
LIBS_SRC  := #../libs/src/HTTPStatusCodes.c ../libs/src/cJSON.c ../libs/src/md5.c

ALL_SRC := $(SRC) $(CORE_SRC) $(UTILS_SRC) $(LIBS_SRC)

# Find all .c source files recursively

# Temp dirs to be deleted on clean
CACHE_DIR := data/cache

# Compilation flags
# CFLAGS := -std=c99 -MMD -MP -Wall -Wextra -Werror -Wfatal-errors -Wno-format-truncation -I../libs
# CFLAGS := -std=c99 -MMD -MP -g -I../libs -I../utils
CFLAGS := -std=c99 -MMD -MP -Wall -Wextra -Wfatal-errors -g -Iinclude -I../libs/include -I../core/utils/include -I../core/modules/include
ASAN_FLAGS := -O1 -g3 -fno-omit-frame-pointer -fsanitize=address,undefined

# Linker flags (auto-detect platform)
LDFLAGS := -flto -Wl,--gc-sections

# Linked libraries
LIBS := -lcurl

#Fuzzing build
FUZZ_CC     ?= afl-gcc-fast
FUZZ_CFLAGS ?= -fsanitize=undefined -g -DHTTP_FUZZ_MODE
FUZZ_OUT    ?= ../fuzz_http

FUZZ_SOURCES = \
    src/http/http_connection.c \
    src/http/http_parser.c \
    src/http/http_server.c \
    src/tcp/tcp_client.c \
    src/tcp/tcp_server.c \
    src/scheduler.c \
    src/weather/weather_instance.c \
    src/weather/weather_server.c \
    ../utils/src/linked_list.c \
		../utils/src/misc_utils.c \
    ../utils/src/file_utils.c \
    ../utils/src/json_utils.c \
    ../utils/src/time_utils.c \
    ../libs/src/yuarel.c \
    ../libs/src/HTTPStatusCodes.c \
    ./tests/fuzz_http.c

#Asan flags for fsanitize=address on Arch
ASAN_CC     ?= gcc
ASAN_OUT    ?= ../fuzz_http_asan

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

fuzz:
	$(FUZZ_CC) $(FUZZ_CFLAGS) $(FUZZ_SOURCES) \
	    -Iinclude \
	    -I../utils/include \
	    -I../libs/include \
	    -o $(FUZZ_OUT)
	cd .. && afl-fuzz -i ./tests/inputs -o ./tests/findings -- ../fuzz_http

fuzz-asan:
	$(ASAN_CC) $(ASAN_CFLAGS) $(FUZZ_SOURCES) \
	    -Iinclude \
	    -I../utils/include \
	    -I../libs/include \
	    -o $(ASAN_OUT)
	@echo ""
	@echo "Built ASan binary: $(ASAN_OUT)"
	@echo "Exempel: ./fuzz_http_asan < findings/default/crashes/ID_FILE"

# run-asan: asan
# 	@echo "Running ASan build..."
# 	@./$(BIN)
#
# asan:
# 	@$(MAKE) CC=$(CC) CFLAGS="$(CFLAGS) $(ASAN_FLAGS)" LDFLAGS="$(LDFLAGS) $(ASAN_FLAGS)" all

print:
	@echo "Source files: $(SRC)"
	@echo "Object files: $(OBJ)"
	@echo "Dependency files: $(DEP)"

-include $(DEP)

# ==================== Phonies ====================

.PHONY: all run clean print gdb valgrind
