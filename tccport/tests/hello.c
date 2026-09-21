#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv) {
    printf("Hello from TinyCC running inside the kernel! argc=%d argv[0]=%s\n", argc, argv[0]);
    int sum = 0;
    for (int i = 1; i <= 100; i++) sum += i;
    printf("sum(1..100) = %d, hex=%x, neg=%d, padded=[%5d] [%-5d] [%05d]\n", sum, 0xdeadbeef, -42, 42, 42, 42);
    char *p = malloc(32); strcpy(p, "heap ok"); printf("%s (len %d)\n", p, (int)strlen(p)); free(p);
    return sum == 5050 ? 0 : 1;
}
