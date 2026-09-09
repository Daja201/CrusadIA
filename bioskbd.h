#ifndef BIOSKBD_H
#define BIOSKBD_H
int bios_has_char(void);
char bios_getchar_echo(void);
void vesa_print_char(char c);
char bios_getchar_nonblocking(void);
#endif
