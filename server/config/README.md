# Configuration file manual (key=value)

This is a practical manual for writing and using the project’s `key=value` config files with the minimal `config_handler` parser.

## 1) Writing a config file

### Basic rule

Each setting is one line:

```properties
some.key=value
```

Keys are case-sensitive.

### Comments

Use `#` for comments:

```properties
# full-line comment
http.port=10580 # inline comment (recommended style)
```

Important: inline comments are only stripped when `#` is at the start of the value, or preceded by whitespace.

```properties
value=abc#def      # the '#def' part is kept (NOT treated as comment)
value=abc #def     # the '#def' part is removed (treated as comment)
```

### Whitespace

Leading/trailing whitespace around keys and values is ignored, so these are equivalent:

```properties
http.port=10580
http.port = 10580
http.port= 10580
```

### Invalid lines

- Empty lines are ignored.
- Lines without `=` are ignored.

### Duplicate keys

If the same key appears multiple times, the **first occurrence wins** (later duplicates will not override it when reading).

### Keep lines short

Lines are read into a 512-byte buffer. Keep each `key=value` line well under ~500 characters to avoid truncation.

## 2) Common value conventions

The config loader returns values as **strings**. Conventions below are what your code should expect and validate:

- Integers: decimal, e.g. `10580`, `256`, `1048576`
- Booleans: `true` / `false` (lowercase)
- Paths: `logs/app.log`, `/var/www/html`
- Lists: comma-separated, e.g. `10.0.0.1,10.0.0.2,10.0.0.3`

If you need stronger rules (ranges, allowed enums, required keys), enforce them in the caller after reading.

## 3) Reading config values in C

The API reads multiple keys in one call:

```c
int config_get_value(const char* config_path,
                     const char* keys[],
                     char* values[],
                     size_t max_value_len,
                     size_t key_count);
```

Return codes:

- `0`: success (all requested keys were found)
- `-1`: file open/read error
- `-2`: missing key(s) or invalid arguments

### Example: read one key with a default

```c
const char* keys[] = {"http.port"};
char port_buf[16] = {0};
char* values[] = {port_buf};

snprintf(port, sizeof(port), "%s", "10580"); /* default */

if (config_get_value("config/system.conf", keys, values, sizeof(port_buf), 1) == 0) {
  if (port_buf[0] != 0) {
    snprintf(port, sizeof(port), "%s", port_buf);
  }
}
```

### Example: read multiple required keys

```c
const char* keys[] = {
  "worker.enabled",
  "worker.threads",
  "worker.queue_size",
};

char enabled[8] = {0};
char threads[16] = {0};
char queue_size[16] = {0};
char* values[] = { enabled, threads, queue_size };

int rc = config_get_value("config/system.conf", keys, values, 16, 3);
if (rc != 0) {
  /* handle missing keys / file error */
}
```

## 4) Path handling (important)

`config_path` is passed to `fopen()` as-is. If you provide a relative path (like `config/system.conf`), it is resolved relative to the process **current working directory**.

Practical options:

- Run the program from the directory that makes your relative path correct.
- Use an absolute path.
- Build the config path at runtime (for example from an env var) and pass that to `config_get_value`.

## 5) Recommended key naming

Use dot-separated namespaces:

- `http.port`, `http.enabled`
- `tcp.port`, `tcp.backlog`
- `limits.max_connections`
- `log.level`, `log.path`

This keeps configs readable and avoids collisions.

## 6) Troubleshooting checklist

- Keys not found (`-2`)
  - Check spelling/case.
  - Ensure the key appears *once* (or the intended value is the first occurrence).
  - Ensure the line contains `=`.

- Values include unexpected `#...`
  - Remember `value=abc#def` keeps `#def`. Use `value=abc #comment` for an inline comment.

- File not found (`-1`)
  - Verify the working directory and the relative path you passed to `config_get_value`.
