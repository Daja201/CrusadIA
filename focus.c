#include "focus.h"

#define MAX_TARGETS 16
#define MAX_STACK 8

typedef struct {
    key_handler_t handler;
    void *ctx;
    const char *name;
    int used;
} target_t;

static target_t targets[MAX_TARGETS];
static int target_count = 0;

static int focus_stack[MAX_STACK];
static int focus_depth = 0;

int focus_register(key_handler_t handler, void *ctx, const char *name) {
    for (int i = 0; i < MAX_TARGETS; i++) {
        if (!targets[i].used) {
            targets[i].handler = handler;
            targets[i].ctx = ctx;
            targets[i].name = name;
            targets[i].used = 1;
            if (i >= target_count) target_count = i + 1;
            return i;
        }
    }
    return -1;
}

void focus_push(int id) {
    if (id < 0 || id >= target_count || !targets[id].used) return;
    if (focus_depth < MAX_STACK) focus_stack[focus_depth++] = id;
}

void focus_pop(void) {
    if (focus_depth > 0) focus_depth--;
}

int focus_active(void) {
    return focus_depth > 0;
}

void focus_dispatch(char c) {
    if (focus_depth == 0) return;
    int id = focus_stack[focus_depth - 1];
    if (targets[id].used) targets[id].handler(c, targets[id].ctx);
}