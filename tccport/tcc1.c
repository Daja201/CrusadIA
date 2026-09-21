/* TinyCC's own runtime helpers (64-bit division/shifts, float<->long long).
 * Code compiled by TinyCC for i386 calls these by their standard names, but
 * libdiv.c already defines some of them, so build them under a tcc1_ prefix
 * and hand them to the compiled program through tcc_add_symbol(). */
#define __ashldi3     tcc1___ashldi3
#define __ashrdi3     tcc1___ashrdi3
#define __divdi3      tcc1___divdi3
#define __fixdfdi     tcc1___fixdfdi
#define __fixsfdi     tcc1___fixsfdi
#define __fixunsdfdi  tcc1___fixunsdfdi
#define __fixunssfdi  tcc1___fixunssfdi
#define __fixunsxfdi  tcc1___fixunsxfdi
#define __fixxfdi     tcc1___fixxfdi
#define __floatundidf tcc1___floatundidf
#define __floatundisf tcc1___floatundisf
#define __floatundixf tcc1___floatundixf
#define __lshrdi3     tcc1___lshrdi3
#define __moddi3      tcc1___moddi3
#define __udivdi3     tcc1___udivdi3
#define __umoddi3     tcc1___umoddi3
#include "../tinycc/lib/libtcc1.c"
