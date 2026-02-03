#include "config/config_handler.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

//helpers
static void trim(char *s) {
    char *p = s;
    while (isspace(*p)) p++;
    memmove(s, p, strlen(p) + 1);

    size_t len = strlen(s);
    while (len && isspace(s[len - 1]))
        s[--len] = 0;
}

//core function

int config_get_value(
    const char *config_path,
    const char *keys[],
    char *values[],
    size_t max_value_len,
    size_t key_count
) {
    FILE *f = fopen(config_path, "r");
    if (!f)
        return -1;

    int found[key_count];
    memset(found, 0, sizeof(found));

    char line[512];

    while (fgets(line, sizeof(line), f)) {
        trim(line);

        if (line[0] == '#' || line[0] == 0)
            continue;

        char *eq = strchr(line, '=');
        if (!eq)
            continue;

        *eq = 0;
        char *key = line;
        char *value = eq + 1;

        trim(key);
        trim(value);

        for (size_t i = 0; i < key_count; i++) {
            if (!found[i] && strcmp(keys[i], key) == 0) {
                strncpy(values[i], value, max_value_len - 1);
                values[i][max_value_len - 1] = 0;
                found[i] = 1;
            }
        }
    }

    fclose(f);

    for (size_t i = 0; i < key_count; i++) {
        if (!found[i])
            return -2;
    }

    return 0;
}