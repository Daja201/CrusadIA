#include "bioskbd.h"
#include "font.h"

int vesa_cursor_x = 0;
int vesa_cursor_y = 0;
static int shift_pressed = 0;
static int is_extended = 0;

static inline unsigned char inb(unsigned short port)
{
    unsigned char ret;
    asm volatile ("inb %1, %0" : "=a" (ret) : "Nd" (port));
    return ret;
}

int bios_has_char(void) {
    return (inb(0x64) & 1);
}

static unsigned char read_scancode(void) {
    return inb(0x60);
}

static const char map_lower[128] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,'a','s','d','f','g','h','j','k','l',';','\'','`',0,'\\','z',
    'x','c','v','b','n','m',',','.','/',0, '*',0,' ',
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    '7', '8', '9', '-', '4', '5', '6', '+', '1', '2', '3', '0', '.'
};

static const char map_upper[128] = {
    0, 27, '!','@','#','$','%','^','&','*','(',')','_','+','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,'A','S','D','F','G','H','J','K','L',':','"','~',0,'|','Z',
    'X','C','V','B','N','M','<','>','?',0, '*',0,' ',
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    '7', '8', '9', '-', '4', '5', '6', '+', '1', '2', '3', '0', '.'
};


char bios_getchar_echo(void) {
   return bios_getchar_nonblocking();
}

char bios_getchar_nonblocking(void) {
    if (!bios_has_char()) return 0;
    unsigned char sc = read_scancode();
    char c = 0;

    if (sc == 0xE0) { is_extended = 1; return 0; }

    if (sc & 0x80) {
        unsigned char released = sc & 0x7F;
        if (released == 0x2A || released == 0x36) shift_pressed = 0;
        is_extended = 0;
        return 0;
    }

    if (sc == 0x2A || sc == 0x36) { shift_pressed = 1; return 0; }

    if (is_extended) {
        is_extended = 0;
        if (sc == 0x48) return (char)1;
        if (sc == 0x50) return (char)2;
        return 0;
    }

    if (sc < 128) {
        c = shift_pressed ? map_upper[sc] : map_lower[sc];
    }
    if (c != 0) {
        return c;
    }
    return 0;
}