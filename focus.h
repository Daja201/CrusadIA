#ifndef FOCUS_H
#define FOCUS_H

typedef void (*key_handler_t)(char c, void *ctx);

int focus_register(key_handler_t handler, void *ctx, const char *name);
void focus_push(int id);
void focus_pop(void);
int focus_active(void);
void focus_dispatch(char c);

#endif