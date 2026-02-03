#include "config/config_handler.h"  // declares config_get_value(...)
#include <stdio.h>                  // FILE, fopen, fgets, fclose
#include <string.h>                 // strlen, memmove, memset, strchr, strcmp, strncpy
#include <ctype.h>                  // isspace

// helpers
static void trim(char *s) {                 // removes leading/trailing whitespace in-place
    char *p = s;                            // p will walk forward to the first non-space
    while (isspace(*p)) p++;                // skip leading spaces/tabs/newlines
    memmove(s, p, strlen(p) + 1);           // shift the remaining text to the start (+1 copies '\0')

    size_t len = strlen(s);                 // current length after removing leading whitespace
    while (len && isspace(s[len - 1]))      // while there is trailing whitespace...
        s[--len] = 0;                       // ...shrink string and write terminating '\0'
}

// core function

int config_get_value(                      // reads multiple key=value pairs from a config file
    const char *config_path,               // path to config file (e.g. "server/config/system.conf")
    const char *keys[],                    // array of keys to look up
    char *values[],                        // array of output buffers (one per key)
    size_t max_value_len,                  // max bytes to copy into EACH output buffer
    size_t key_count                       // how many keys/values pairs are requested
) {
    FILE *f = fopen(config_path, "r");     // open config file for reading
    if (!f)                                // if open failed...
        return -1;                         // ...return -1 to signal file error

    int found[key_count];                  // flags: found[i] == 1 when keys[i] has been seen in the file
    memset(found, 0, sizeof(found));       // initialize all found flags to 0

    char line[512];                        // buffer for reading one line at a time

    while (fgets(line, sizeof(line), f)) { // read next line; returns NULL at EOF/error
        trim(line);                        // remove surrounding whitespace from the line

        if (line[0] == '#' || line[0] == 0) // skip comments (#...) and empty lines
            continue;                      // move to next line

        char *eq = strchr(line, '=');      // find the '=' that separates key and value
        if (!eq)                           // if no '=', the line is not a key=value pair
            continue;                      // ignore it

        *eq = 0;                           // replace '=' with '\0' so 'line' becomes a C string key
        char *key = line;                  // key starts at the beginning of the line buffer
        char *value = eq + 1;              // value starts right after where '=' used to be

        trim(key);                         // trim whitespace around key
        trim(value);                       // trim whitespace around value

        for (size_t i = 0; i < key_count; i++) {               // scan requested keys...
            if (!found[i] && strcmp(keys[i], key) == 0) {      // if this line matches keys[i]...
                strncpy(values[i], value, max_value_len - 1);  // copy value into caller buffer
                values[i][max_value_len - 1] = 0;              // force NUL-termination
                found[i] = 1;                                  // mark this key as found
            }
        }
    }

    fclose(f);                             // close the config file

    for (size_t i = 0; i < key_count; i++) { // ensure every requested key was found
        if (!found[i])                     // if any key was never found...
            return -2;                     // ...return -2 to signal missing key(s)
    }

    return 0;                              // success: all requested keys were found and copied
}