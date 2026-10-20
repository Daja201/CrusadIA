#include "env.h"
#include "string.h"
#include <stddef.h>

static char s_env[ENV_MAX_VARS][ENV_ENTRY_MAX];
static int s_env_used[ENV_MAX_VARS];
static int s_env_count = 0;

static int find_index(const char* key) {
    size_t len = strlen(key);
    for (int i = 0; i < ENV_MAX_VARS; i++) {
        if (!s_env_used[i]) continue;
        if (strncmp(s_env[i], key, len) == 0 && s_env[i][len] == '=') return i;
    }
    return -1;
}

void env_init(void) {
    for (int i = 0; i < ENV_MAX_VARS; i++) s_env_used[i] = 0;
    s_env_count = 0;
    env_set("PATH", "/");
    env_set("HOME", "/");
    env_set("SHELL", "/kernel");
    env_set("TERM", "crusadia");
}

int env_set(const char* key, const char* value) {
    int idx = find_index(key);
    if (idx < 0) {
        idx = -1;
        for (int i = 0; i < ENV_MAX_VARS; i++) {
            if (!s_env_used[i]) { idx = i; break; }
        }
        if (idx < 0) return -1;
        s_env_used[idx] = 1;
        s_env_count++;
    }

    size_t klen = strlen(key);
    size_t vlen = strlen(value);
    if (klen + 1 + vlen + 1 > ENV_ENTRY_MAX) return -1;

    strcpy(s_env[idx], key);
    s_env[idx][klen] = '=';
    strcpy(s_env[idx] + klen + 1, value);
    return 0;
}

int env_unset(const char* key) {
    int idx = find_index(key);
    if (idx < 0) return -1;
    s_env_used[idx] = 0;
    s_env_count--;
    return 0;
}

const char* env_get(const char* key) {
    int idx = find_index(key);
    if (idx < 0) return NULL;
    size_t klen = strlen(key);
    return s_env[idx] + klen + 1;
}

int env_count(void) {
    return s_env_count;
}

const char* env_entry(int index) {
    int seen = 0;
    for (int i = 0; i < ENV_MAX_VARS; i++) {
        if (!s_env_used[i]) continue;
        if (seen == index) return s_env[i];
        seen++;
    }
    return NULL;
}
