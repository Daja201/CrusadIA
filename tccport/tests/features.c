#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <os.h>
#include <time.h>
static int cmp(const void *a, const void *b) { return *(const int*)a - *(const int*)b; }
static void say(const char *fmt, ...) { va_list ap; va_start(ap, fmt); char b[128]; vsnprintf(b, sizeof b, fmt, ap); va_end(ap); klog(b); }
static void deep(int n) { if (n == 0) { printf("exiting from depth 50 with 7\n"); exit(7); } deep(n - 1); }
int main(void) {
    int64_t a = 1234567890123LL, b = -98765LL;
    printf("i64: %lld / %lld = %lld, %% = %lld, u=%llu\n", a, b, a / b, a % b, (uint64_t)a * 1000);
    double d = 3.14159265358979; float f = 2.5f; long double ld = 1e-3L;
    printf("float: %.4f %.2f %.6f sci-const=%.1f\n", d, (double)f, (double)ld, 6.02e3);
    printf("cast: %d %d\n", (int)(d * 100), (int)(-2.7));
    int v[5] = {5, 3, 9, 1, 7}; qsort(v, 5, sizeof v[0], cmp);
    printf("sorted: %d %d %d %d %d\n", v[0], v[1], v[2], v[3], v[4]);
    bool ok = strstr("kernel compiler", "compiler") != 0; printf("bool=%d\n", ok);
    say("say(%s,%d)\n", "x", 5);
    klogf("klogf direct: %d %s\n", 99, "ok");
    vesa_draw_rec(10, 20, 30, 40, 0xff00ff); printf("screen %ux%u ticks=%u\n", Wwidth(), Hheight(), system_ticks);
    time_t t = time(0); struct tm *tm = localtime(&t); printf("time: %04d-%02d-%02d %02d:%02d:%02d\n", tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
    printf("__DATE__=%s __TIME__=%s\n", __DATE__, __TIME__);
    deep(50);
    printf("NOT REACHED\n");
    return 0;
}
