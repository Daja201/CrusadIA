#ifndef TEMPLAR_H
#define TEMPLAR_H
#include <stdint.h>

typedef void (*templar_key_handler_t)(char c);
extern templar_key_handler_t g_active_key_handler;

void templar_init(void);

void templar_run_source(const char *src);

void templar_run_file(const char *filename);

#endif