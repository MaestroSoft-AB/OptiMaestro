#indef CONFIG_HANDLER_H
#define CONFIG_HANDLER_H

#include <stddef.h>

int config_get_value(
    const char *config_path,
    const char *key[],
    char *values[],
    size_t max_value_len
    size_t key_count
)

#endif