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
RUN_DIRS := /var/lib/maestro/ /etc/maestro/ /run/maestro
RUN_FILES := /var/log/maestro.log /run/optimizer.pid
CONF_FILE := /etc/maestro/optimizer.conf
INSTALL_SCRIPT := ../config/install.sh

# ----- Local source files -----
SRC_DIR 			 := src
COMMON_SRC_DIR := ../common/src

SRC 			 := $(shell find -L $(SRC_DIR) -type f -name '*.c')
COMMON_SRC := $(shell cd $(COMMON_SRC_DIR) && find -L . -type f -name '*.c')
COMMON_SRC := $(patsubst ./%,%,$(COMMON_SRC))

# ----- MaestroCore (submodule) -----
MAESTRO_DIR ?= ../external/MaestroCore
MAESTRO_LIB := $(MAESTRO_DIR)/build/lib/libmaestrocore.a
CJSON_DIR 	:= ../external/cJSON # Needed this for local include
CORE_FLAGS  := -I$(MAESTRO_DIR)/modules/include \
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
CACHE_DIR := /var/cache/maestro/optimizer 

# Compilation flags
CFLAGS := -std=c99 -MMD -MP -Wall -Wextra -Wfatal-errors -g -pg \
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
COMMON_OBJ := $(patsubst %.c,$(BUILD_DIR)/common/src/%.o,$(COMMON_SRC))

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
	@$(CC) $(LDFLAGS) $(OBJ) $(MAESTRO_LIB) $(MBEDTLS_LIBS) -o $@ $(LIBS) -lm

# asan: clean
# 	@echo "Building ASan binary..."
# 	@$(MAKE) CC=$(CC) CFLAGS="$(CFLAGS) $(ASAN_FLAGS)" LDFLAGS="$(LDFLAGS) $(ASAN_FLAGS)" all

$(BUILD_DIR)/$(SRC_DIR)/%.o: $(SRC_DIR)/%.c
	@echo "Compiling SRC $<..."
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/common/src/%.o: $(COMMON_SRC_DIR)/%.c
	@echo "Compiling COMMON_SRC $<..."
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

# ==================== Daemon =====================
DAEMON_BIN := /usr/local/bin/optimizer-daemon
SERVICE_FILE := optimizer-daemon.service

daemon-install: CFLAGS += -DDAEMONIZE
daemon-install: $(OBJ) $(MAESTRO_LIB) $(SERVICE_FILE)
	@echo "Building optimizer-daemon..."
	@$(CC) $(LDFLAGS) $(OBJ) $(MAESTRO_LIB) $(MBEDTLS_LIBS) -o optimizer-daemon $(LIBS) -lm
	@echo "Installing daemon binary to $(DAEMON_BIN)..."
	@sudo install -m 0755 optimizer-daemon $(DAEMON_BIN)
	@sudo chown root:root $(DAEMON_BIN)
	@rm -f optimizer-daemon
	@echo "Ensuring system user/group 'maestro' exists..."
	@sudo id -u maestro >/dev/null 2>&1 || sudo useradd --system --no-create-home --shell /usr/sbin/nologin maestro
	@echo "Creating runtime dirs and log file..."
	@sudo mkdir -p $(RUN_DIRS)
	@sudo touch $(RUN_FILES)
	@sudo chown -R maestro:maestro $(RUN_DIRS) $(RUN_FILES)
	@echo "Installing systemd service..."
	@sudo install -m 0644 $(SERVICE_FILE) /etc/systemd/system/$(SERVICE_FILE)
	@sudo systemctl daemon-reload
	@sudo systemctl enable optimizer-daemon.service
	@echo "Done. Start with:"
	@echo "  sudo systemctl start optimizer-daemon.service"

daemon-clean:
	@echo "Stopping and disabling optimizer-daemon service..."
	@sudo systemctl stop $(SERVICE_FILE) 2>/dev/null || true
	@sudo systemctl disable $(SERVICE_FILE) 2>/dev/null || true
	@echo "Removing systemd service file..."
	@sudo rm -f /etc/systemd/system/$(SERVICE_FILE)
	@sudo systemctl daemon-reload
	@echo "Removing daemon binary..."
	@sudo rm -f $(DAEMON_BIN)
	@echo "Cleaning up runtime and configuration directories..."
	@echo "Optionally removing 'maestro' user (comment out if shared)..."
	@sudo userdel maestro 2>/dev/null || true
	@echo "Daemon clean complete."

# ==================== Phonies ====================

.PHONY: all run clean print gdb valgrind asan run-asan install daemon-install daemon-clean
