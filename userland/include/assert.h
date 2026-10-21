#ifndef USERLAND_ASSERT_H
#define USERLAND_ASSERT_H

#ifdef NDEBUG
#define assert(x) ((void)0)
#else
void __assert_fail_crusadia(const char* expr, const char* file, int line);
#define assert(x) ((x) ? (void)0 : __assert_fail_crusadia(#x, __FILE__, __LINE__))
#endif

#endif
