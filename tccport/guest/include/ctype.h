#ifndef _GUEST_CTYPE_H
#define _GUEST_CTYPE_H
#define isdigit(c)  ((c) >= '0' && (c) <= '9')
#define isupper(c)  ((c) >= 'A' && (c) <= 'Z')
#define islower(c)  ((c) >= 'a' && (c) <= 'z')
#define isalpha(c)  (isupper(c) || islower(c))
#define isalnum(c)  (isalpha(c) || isdigit(c))
#define isspace(c)  ((c) == ' ' || ((c) >= 9 && (c) <= 13))
#define isprint(c)  ((c) >= 0x20 && (c) < 0x7F)
#define isxdigit(c) (isdigit(c) || ((c) >= 'a' && (c) <= 'f') || ((c) >= 'A' && (c) <= 'F'))
#define toupper(c)  (islower(c) ? (c) - 32 : (c))
#define tolower(c)  (isupper(c) ? (c) + 32 : (c))
#endif
