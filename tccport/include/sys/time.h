#ifndef TCCPORT_SYS_TIME_H
#define TCCPORT_SYS_TIME_H
#include <time.h>
struct timeval { long tv_sec; long tv_usec; };
int gettimeofday(struct timeval *tv, void *tz);
#endif
