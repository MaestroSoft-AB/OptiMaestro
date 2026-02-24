# =================================================
# ============== Optimizer Makefile ===============
# =================================================

# Dirs/files to install and take proper ownership of:
# /var/lib/maestro/ 
# /var/log/maestro.log
# /etc/maestro/optimizer.conf

# Compiler
CC := gcc

BUILD_DIR := build
USER := $(shell whoami)
UID := $(shell id -u)
GID := $(shell id -g)
BIN_DIR	 := /home/$(USER)/.local/bin
RUN_DIRS := /var/lib/maestro/ /etc/maestro/ 
RUN_FILES := /var/log/maestro.log
CONF_FILE := /etc/maestro/optimizer.conf
INSTALL_SCRIPT := ./scripts/install.sh

# ----- Local source files -----
SRC_DIR 			 := src
COMMON_SRC_DIR := ../common/src

SRC 			 := $(shell find -L $(SRC_DIR) -type f -name '*.c')
COMMON_SRC := $(shell find -L $(COMMON_SRC_DIR) -type f -name '*.c')

# ----- MaestroCore (submodule) -----
MAESTRO_DIR ?= ../external/MaestroCore
MAESTRO_LIB := $(MAESTRO_DIR)/build/lib/libmaestrocore.a
CJSON_DIR 	:= ../external/cJSON # Needed this for local include
CORE_FLAGS := -I$(MAESTRO_DIR)/modules/include \
              -I$(MAESTRO_DIR)/utils/include \
              -L$(MAESTRO_DIR)/build/lib \
              -lmaestrocore -lpthread -lm
MBEDTLS_LIBS := \
  $(MAESTRO_DIR)/build/anonymaestro/library/libmbedtls.a \
  $(MAESTRO_DIR)/build/anonymaestro/library/libmbedx509.a \
  $(MAESTRO_DIR)/build/anonymaestro/library/libmbedcrypto.a

# Enable MaestroCore JSON (optional): make JSON=1
JSON ?= 1

# Temp dirs
CACHE_DIR := data/cache # TODO: Change to /var/cache/maestro/optimizer

# Compilation flags
CFLAGS := -std=c99 -MMD -MP -Wall -Wextra -Wfatal-errors -g \
	-Iinclude \
	-I$(MAESTRO_DIR)/modules/include \
	-I$(MAESTRO_DIR)/utils/include \
	-I$(MAESTRO_DIR)/external/anonymaestro/include \
	-I../common/include \
	$(CORE_FLAGS)

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
	@echo "Creating bin dir: $(BIN_DIR)..."
	@mkdir -p $(BIN_DIR)
	@echo "Linking..."
	@$(CC) $(LDFLAGS) $(OBJ) $(MAESTRO_LIB) $(MBEDTLS_LIBS) -o $@ $(LIBS) -lpthread -lm

# asan: clean
# 	@echo "Building ASan binary..."
# 	@$(MAKE) CC=$(CC) CFLAGS="$(CFLAGS) $(ASAN_FLAGS)" LDFLAGS="$(LDFLAGS) $(ASAN_FLAGS)" all

$(BUILD_DIR)/$(SRC_DIR)/%.o: $(SRC_DIR)/%.c
	@echo "Compiling $<..."
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c $< -o $@

install: 
	@echo "Creating system dirs and files..."
	@chmod +x $(INSTALL_SCRIPT)
	@sudo sh $(INSTALL_SCRIPT)
	@sudo mkdir -p $(RUN_DIRS)
	@sudo touch $(RUN_FILES)
	@sudo chown -R $(UID):$(GID) $(RUN_DIRS) $(RUN_FILES) 
	@echo "System dirs and files created and chowned."
	@echo "Dirs: $(RUN_DIRS)" 
	@echo "Files: $(RUN_FILES)"

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
