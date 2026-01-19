# ======================================
# 🌤 WeatherMaestro Main Makefile 
# ======================================

MODULES := server client client_cpp

#############################
# PHONIES
#############################


.PHONY: all \
	clean \
	$(MODULES) \
	$(addsuffix /run,$(MODULES)) \
	$(addsuffix /clean,$(MODULES)) \
	$(addsuffix /valgrind,$(MODULES)) \
	$(addsuffix /gdb,$(MODULES)) \
	$(addsuffix /fuzz,$(MODULES)) \
	$(addsuffix /fuzz-asan,$(MODULES)) \


#############################
# Recipes
#############################


# Default target: build all modules
all: $(MODULES)

clean:
	@for module in $(MODULES); do \
		echo "Cleaning $$module..."; \
		$(MAKE) -C $$module clean; \
	done
	@echo "All modules cleaned."

# Build each module with symlinks created first
$(MODULES):
	@echo "Building module $@..."
	$(MAKE) -C $@ all

# Run target using make [module]/run
$(addsuffix /run,$(MODULES)):
	@MODULE=$(@D); \
	echo "Running module $$MODULE..."; \
	$(MAKE) -C $$MODULE run

# Clean target using make [module]/clean
$(addsuffix /clean,$(MODULES)):
	@MODULE=$(@D); \
	echo "Cleaning module $$MODULE..."; \
	$(MAKE) -C $$MODULE clean

# Run valgrind on target using [module]/valgrind
$(addsuffix /valgrind,$(MODULES)):
	@MODULE=$(@D); \
	echo "Debugging module $$MODULE using valgrind..."; \
	$(MAKE) -C $$MODULE valgrind

# Run gdb on target using [module]/gdb
$(addsuffix /gdb,$(MODULES)):
	@MODULE=$(@D); \
	echo "Debugging module $$MODULE using gdb..."; \
	$(MAKE) -C $$MODULE gdb 

$(addsuffix /fuzz,$(MODULES)):
	@MODULE=$(@D); \
	echo "Building fuzz target for $$MODULE..."; \
	$(MAKE) -C $$MODULE fuzz

$(addsuffix /fuzz-asan,$(MODULES)):
	@MODULE=$(@D); \
	echo "Building ASan fuzz binary for $$MODULE..."; \
	$(MAKE) -C $$MODULE fuzz-asan

$(addsuffix /run-asan,$(MODULES)):
	@MODULE=$(@D); \
	echo "Debugging module $$MODULE using asan..."; \
	$(MAKE) -C $$MODULE run-asan
