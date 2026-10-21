#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

void __assert_fail_crusadia(const char* expr, const char* file, int line) {
    char buf[256];
    snprintf(buf, sizeof(buf), "assertion failed: %s (%s:%d)\n", expr, file, line);
    write(STDERR_FILENO, buf, strlen(buf));
    abort();
}
