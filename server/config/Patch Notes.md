# Patch Notes (Config Modularization)

Date: 2026-03-09

Branch: `shayan_config_hanler` (local working tree)

## Goal

Reduce boilerplate when reading `key=value` config files so call sites don’t have to repeat this pattern:

- build `keys[]` / `values[]`
- allocate temporary buffers
- write defaults + override logic

…and also make later config usage easier (typed reads, defaults, and “load once, query many times”).

## Summary of Changes (by file)

### external/MaestroCore/utils/include/maestroutils/config_handler.h

**What changed**

- Added C++ compatibility guards:
  - `#ifdef __cplusplus` / `extern "C" {` / `}`
- Added a new modular API on top of the existing `config_get_value()`:
  - `typedef struct config_handler config_handler_t;`
  - `config_handler_load()` / `config_handler_free()`
  - `config_handler_get()` / `config_handler_get_default()`
  - Typed helpers:
    - `config_handler_get_int_default()`
    - `config_handler_get_double_default()`
    - `config_handler_get_bool_default()`

**Why**

- Keeps the original bulk API (`config_get_value`) intact for existing callers.
- Enables a simpler, more modular usage style:
  - Load the config file once.
  - Ask for keys as needed with defaults.
  - Parse values strictly when you want int/double/bool.

### external/MaestroCore/utils/src/config_handler.c

**What changed**

- Kept the original `config_get_value()` implementation (no behavioral change intended).
- Implemented the new modular API declared in the header:
  - Added internal linked-list storage (`config_entry`) and an owning config object (`struct config_handler`).
  - Implemented `config_handler_load()`:
    - Reads the file once.
    - Applies the same trimming and inline-comment logic as `config_get_value()`.
    - Stores the **first occurrence** of each key (same rule as the README describes).
  - Implemented `config_handler_free()` to release all allocated memory.
  - Implemented `config_handler_get()` and `config_handler_get_default()`.
  - Implemented strict parsing helpers:
    - `parse_int_strict()` via `strtol()` with full-string validation and `INT_MIN/INT_MAX` bounds.
    - `parse_double_strict()` via `strtod()` with full-string validation.
    - Bool parsing for `true/false` and also `1/0`.
- Added required includes to support the above:
  - `<stdlib.h>`, `<errno.h>`, `<limits.h>`

**Why**

- The old API forces the caller to manage multiple buffers and arrays, even for a single value.
- Loading once avoids repeated file I/O and makes it natural to read many settings.
- Strict parsing gives you a single place to enforce “valid int/double/bool” instead of `atoi/atof` silently accepting junk.

### server/src/http/http_server.c

**What changed**

- Removed the single-key `config_get_value()` boilerplate (`keys[]`, `values[]`, temp buffer).
- Now does:
  - default `_HTTPServer->port` to `10580`
  - `config_handler_load("config/system.conf")`
  - `config_handler_get_default(cfg, "http.port", "10580")`
  - `config_handler_free(cfg)`

**Why**

- Preserves the same default behavior.
- Makes adding more HTTP settings later straightforward (same loaded `cfg`, more `config_handler_get_*` calls).

### optimizer/src/optimizer.c

**What changed**

- Added `<stdlib.h>` include (needed for `malloc`, etc.).
- Added input validation: return `ERR_INVALID_ARG` if `_OC` is NULL.
- Replaced the large `keys[]/values[]/buffers` + `config_get_value()` section with the modular approach:
  - Load once: `config_handler_load(&cfg, OPTIMIZER_CONF_PATH)`
  - Preserve the previous “required keys” behavior:
    - Explicitly checks that each previously-requested key exists; otherwise returns `-2`.
  - Uses typed getters for numeric values:
    - `sys.max_threads` (int)
    - `data.spots.price_class` (int)
    - `facility.latitude/longitude` (double → stored as float)
    - `facility.panel.*` (int)
  - Uses `config_handler_get_default()` for directory strings.
  - Ensures `config_handler_free(cfg)` is called on success and on all error exits.

**Why**

- Removes a lot of repetitive buffer plumbing.
- Improves correctness:
  - `atoi/atof` were replaced with strict parsing helpers (via the modular API), preventing silent “0 on error” behavior.
  - Centralizes the parsing rules.
- Keeps behavior compatible where it matters:
  - still requires the same keys to exist (returns `-2` if missing).

### server/config/README.md

**What changed**

- Restored the **Whitespace** note (keys/values are trimmed by the parser).
- Documented both APIs:
  - High-level modular API (`config_handler_load` + `config_handler_get_*`)
  - Low-level bulk API (`config_get_value`)
- Updated examples:
  - Added a clean high-level example that demonstrates defaults + typed parsing.
  - Kept a low-level single-key example showing the old `keys[]/values[]` pattern.

**Why**

- Prevents future contributors from copy/pasting the old boilerplate everywhere.
- Makes it clear which API is preferred and why.
- Preserves the low-level documentation for existing code and for bulk reads.


