#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv, char** envp) {
    printf("hello from a ported app, %d + %d = %d\n", 2, 2, 2 + 2);
    printf("argc=%d\n", argc);
    for (int i = 0; i < argc; i++) {
        printf("argv[%d]=%s\n", i, argv[i]);
    }

    for (int i = 0; envp[i]; i++) {
        printf("envp[%d]=%s\n", i, envp[i]);
    }

    const char* path = getenv("PATH");
    printf("PATH=%s\n", path ? path : "(unset)");

    char* buf = malloc(64);
    if (buf) {
        snprintf(buf, 64, "malloc works: %s", "yes");
        puts(buf);
        free(buf);
    }

    return 0;
}
