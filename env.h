#ifndef ENV_H
#define ENV_H

#define ENV_MAX_VARS   16
#define ENV_ENTRY_MAX  128

void env_init(void);
int env_set(const char* key, const char* value);
int env_unset(const char* key);
const char* env_get(const char* key);
int env_count(void);
const char* env_entry(int index);

#endif
