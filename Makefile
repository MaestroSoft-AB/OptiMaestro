# ======================================
# 🌤 WeatherMaestro Main Makefile
# ======================================

#TODO: Add install option to create folders etc
# /var/lib/maestro for instance with correct ownership
# rootless bins in: /home/$(shell whoami)/.local/bin/maestro
# (ensure path, could be configurable)

MODULES := server client client_cpp optimizer

# --- MaestroCore submodule ---
MAESTRO_DIR := external/MaestroCore

# If you want JSON support in MaestroCore build: make JSON=1
JSON ?= 0

# Add MaestroCore include paths for all subprojects (server/client/...)
# (They must NOT overwrite CFLAGS with := inside their Makefiles.)
export CFLAGS += -I$(MAESTRO_DIR)/modules/include
export CFLAGS += -I$(MAESTRO_DIR)/utils/include

# Optionally also export a define so consumers can #ifdef JSON features
ifeq ($(JSON),1)
export CFLAGS += -DMAESTROUTILS_WITH_CJSON
endif

#############################
# PHONIES
#############################

.PHONY: all clean deps maestro maestro-clean \
	$(MODULES) \
	$(addsuffix /install,$(MODULES)) \
	$(addsuffix /run,$(MODULES)) \
	$(addsuffix /clean,$(MODULES)) \
	$(addsuffix /valgrind,$(MODULES)) \
	$(addsuffix /gdb,$(MODULES)) \
	$(addsuffix /fuzz,$(MODULES)) \
	$(addsuffix /fuzz-asan,$(MODULES)) \
	$(addsuffix /run-asan,$(MODULES)) \
	$(addsuffix /profile,$(MODULES))

#############################
# Recipes
#############################

# Ensure submodules are present
deps:
	@git submodule update --init --recursive

# Build MaestroCore first (respects JSON=1)
maestro: $(MAESTRO_DIR)/build/lib/libmaestrocore.a

$(MAESTRO_DIR)/build/lib/libmaestrocore.a: deps
	@echo "Building MaestroCore (JSON=$(JSON))..."
	@$(MAKE) -C $(MAESTRO_DIR) JSON=$(JSON)

maestro-clean:
	@$(MAKE) -C $(MAESTRO_DIR) clean

# Default target: build MaestroCore + all modules
all: maestro $(MODULES)

clean: maestro-clean
	@for module in $(MODULES); do \
		echo "Cleaning $$module..."; \
		$(MAKE) -C $$module clean; \
	done
	@echo "All modules cleaned."

# Build each module (depends on MaestroCore)
$(MODULES): $(MAESTRO_DIR)/build/lib/libmaestrocore.a
	@echo "Building module $@..."
	$(MAKE) -C $@ all

# Install target using make [module]/install
$(addsuffix /install,$(MODULES)): maestro
	@MODULE=$(@D); \
	echo "Installing module $$MODULE..."; \
	$(MAKE) -C $$MODULE install

# Install target daemon using make [module]/daemon-install
$(addsuffix /daemon-install,$(MODULES)): maestro
	@MODULE=$(@D); \
	echo "Installing module daemon $$MODULE..."; \
	$(MAKE) -C $$MODULE daemon-install

# Clean target daemon using make [module]/daemon-clean
$(addsuffix /daemon-clean,$(MODULES)): maestro
	@MODULE=$(@D); \
	echo "Cleaning module daemon $$MODULE..."; \
	$(MAKE) -C $$MODULE daemon-clean

# Run target using make [module]/run
$(addsuffix /run,$(MODULES)): maestro
	@MODULE=$(@D); \
	echo "Running module $$MODULE..."; \
	$(MAKE) -C $$MODULE run

# Clean target using make [module]/clean
$(addsuffix /clean,$(MODULES)):
	@MODULE=$(@D); \
	echo "Cleaning module $$MODULE..."; \
	$(MAKE) -C $$MODULE clean

# Run valgrind on target using [module]/valgrind
$(addsuffix /valgrind,$(MODULES)): maestro
	@MODULE=$(@D); \
	echo "Debugging module $$MODULE using valgrind..."; \
	$(MAKE) -C $$MODULE valgrind

# Run gdb on target using [module]/gdb
$(addsuffix /gdb,$(MODULES)): maestro
	@MODULE=$(@D); \
	echo "Debugging module $$MODULE using gdb..."; \
	$(MAKE) -C $$MODULE gdb

# Print info
$(addsuffix /print,$(MODULES)): maestro
	@MODULE=$(@D); \
	$(MAKE) -C $$MODULE print

# Fuzz
$(addsuffix /fuzz,$(MODULES)): maestro
	@MODULE=$(@D); \
	echo "Building fuzz target for $$MODULE..."; \
	$(MAKE) -C $$MODULE fuzz

$(addsuffix /fuzz-asan,$(MODULES)): maestro
	@MODULE=$(@D); \
	echo "Building ASan fuzz binary for $$MODULE..."; \
	$(MAKE) -C $$MODULE fuzz-asan

$(addsuffix /run-asan,$(MODULES)): maestro
	@MODULE=$(@D); \
	echo "Debugging module $$MODULE using asan..."; \
	$(MAKE) -C $$MODULE run-asan

# Optimizer daemon
$(addsuffix /daemon,$(MODULES)): maestro
	@MODULE=$(@D); \
	echo "Running $$MODULE as daemon..."; \
	$(MAKE) -C $$MODULE daemon

$(addsuffix /profile,$(MODULES)): maestro
	@MODULE=$(@D); \
	echo "Building profiling target for $$MODULE..."; \
	$(MAKE) -C $$MODULE profile
