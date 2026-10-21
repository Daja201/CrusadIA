#include <time.h>
#include "../syscall.h"

time_t time(time_t* tloc) {
    time_t t = (time_t)_syscall(SYS_GETTIME, 0, 0, 0);
    if (tloc) *tloc = t;
    return t;
}
