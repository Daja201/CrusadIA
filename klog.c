#include "bootinfo.h"
#include "vesa.h"
#include <stdint.h>
#include "klog.h"
#include "string.h"
#include <stdarg.h>
#include "commands.h"
#include "string.h"
#include "diskinfo.h"
#include "fs.h"
#include "pmm.h"
#include "rtc.h"

#define SCREEN_W 1920
#define SCREEN_H 1080

void klog_color(const char* msg, uint32_t color) {
    while (*msg != '\0') {
        char c = *msg;
        if (c == '\n') {
            c_x = 0;
            c_y += 8;
        } else {
            vesa_draw_char_34(c, c_x, c_y, color, 0x000000);
            c_x += 8;
            if (c_x >= SCREEN_W) {
                c_x = 0;
                c_y += 8;
            }
        }
        if (c_y >= SCREEN_H) {
            vesa_scroll(8);
            c_y -= 8;
        }
        msg++;
    }
    cursor('d');
}

void kklog_color(const char* msg, uint32_t color) {
    klog_color(msg, color);
    klog("\n");
}

static void vprintf_internal_color(const char *fmt, uint32_t color, va_list args) {
    char buf[32];
    char ch;
    for (int i = 0; fmt[i] != '\0'; i++) {
        if (fmt[i] != '%') {
            char out[2] = { fmt[i], 0 };
            klog_color(out, color);
            continue;
        }
        i++;
        switch (fmt[i]) {
            case 'd': {
                int val = va_arg(args, int);
                itoa(val, buf, 10);
                klog_color(buf, color);
                break;
            }
            case 'x': {
                uint32_t val = va_arg(args, uint32_t);
                itoa(val, buf, 16);
                klog_color(buf, color);
                break;
            }
            case 's': {
                char *s = va_arg(args, char*);
                if (!s) s = "(null)";
                klog_color(s, color);
                break;
            }
            case 'c': {
                ch = (char)va_arg(args, int);
                char out[2] = { ch, 0 };
                klog_color(out, color);
                break;
            }
            case 'l': {
               if (fmt[i+1] == 'l' && fmt[i+2] == 'u') {
                    unsigned long long v = va_arg(args, unsigned long long);
                    int idx = 0;
                    if (v == 0) {
                        buf[idx++] = '0';
                    } else {
                        unsigned long long div = 1;
                        while (div <= v / 10) div *= 10;
                        while (div > 0) {
                            buf[idx++] = '0' + (v / div);
                            v %= div;
                            div /= 10;
                        }
                    }
                    buf[idx] = '\0';
                    klog_color(buf, color);
                    i += 2;
                } else {
                    klog_color("<?>", color);
                }
                break;
            }
            case '%':
                klog_color("%", color);
                break;
            default:
                klog_color("<?>", color);
                break;
        }
    }
}

void klogf_color(const char *fmt, uint32_t color, ...) {
    va_list args;
    va_start(args, color);
    vprintf_internal_color(fmt, color, args);
    va_end(args);
}

void kklogf_color(const char *fmt, uint32_t color, ...) {
    va_list args;
    va_start(args, color);
    vprintf_internal_color(fmt, color, args);
    va_end(args);
    klog("\n");
}

static void vprintf_internal(const char *fmt, va_list args) {
    char buf[32];
    char ch;
    for (int i = 0; fmt[i] != '\0'; i++) {
        if (fmt[i] != '%') {
            char out[2] = { fmt[i], 0 };
            klog(out);
            continue;
        }
        i++;
        switch (fmt[i]) {
            case 'd': {
                int val = va_arg(args, int);
                itoa(val, buf, 10);
                klog(buf);
                break;
            }
            case 'x': {
                uint32_t val = va_arg(args, uint32_t);
                itoa(val, buf, 16);
                klog(buf);
                break;
            }
            case 's': {
                char *s = va_arg(args, char*);
                if (!s) s = "(null)";
                klog(s);
                break;
            }
            case 'c': {
                ch = (char)va_arg(args, int);
                char out[2] = { ch, 0 };
                klog(out);
                break;
            }
            case 'l': {
               if (fmt[i+1] == 'l' && fmt[i+2] == 'u') {
                    unsigned long long v = va_arg(args, unsigned long long);
                    int idx = 0;
                    if (v == 0) {
                        buf[idx++] = '0';
                    } else {
                        unsigned long long div = 1;
                        while (div <= v / 10) div *= 10;
                        while (div > 0) {
                            buf[idx++] = '0' + (v / div);
                            v %= div;
                            div /= 10;
                        }
                    }
                    buf[idx] = '\0';
                    klog(buf);
                    i += 2;
                } else {
                    klog("<?>");
                }
                break;
            }
            case '%':
                klog("%");
                break;
            default:
                klog("<?>");
                break;
        }
    }
}

void klog(const char* msg) {
    while (*msg != '\0') {
        char c = *msg;

        if (c == '\n') {
            cursor('e');
            c_x = 0;
            c_y += 8;
        }
        else if (c == '\b') {
            cursor('e');
            if (c_x >= 8) {
                c_x -= 8;
                vesa_draw_char_34(' ', c_x, c_y, 0x000000, 0x000000);
            }
            else if (c_y >= 8) {
                c_y -= 8;
                c_x = SCREEN_W - 8;
                vesa_draw_char_34(' ', c_x, c_y, 0x000000, 0x000000);
            }
        }
        else {
            vesa_draw_char_34(c, c_x, c_y, 0xFFFFFF, 0x000000);
            c_x += 8;
            if (c_x >= SCREEN_W) {
                c_x = 0;
                c_y += 8;
                vesa_draw_rec(c_x, c_y, 8, 8, 0x000000);
            }
        }

        if (c_y >= SCREEN_H) {
            vesa_scroll(8);
            c_y -= 8;
        }

        msg++;
    }
    cursor('d');
}

void cursor(char func) {
    if (func == 'e' || func == 'l') {
        vesa_draw_rec(c_x, c_y, 8, 8, 0x000000);
    }
    else {
        vesa_draw_rec(c_x, c_y + 1, 3, 7, 0xFFFF00);
    }
}

void kklog(const char* msg) {
    klog(msg);
    klog("\n");
}

void klogf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintf_internal(fmt, args);
    va_end(args);
}

void kklogf(const char *fmt, ...) {
    klogf(fmt);
    klog("\n");
}

void logo() {
    kklog_color("                                                                                                                        ", 0xFF0000);
    kklog_color("     __                                                                                                                 ", 0xFF0000);
    kklog_color("   ,/ _~.                          |\\                    ,-||-,     -_-/      ------          Welcome to Crusader OS   ", 0xFF0000);
    kklog_color("  (' /|                       _     \\\\                  ('|||  )   (_ /         ||            An hobby operating system", 0xFF0000);
    kklog_color(" ((  ||   ,._-_ \\\\ \\\\  _-_,  < \\,  / \\\\  _-_  ,._-_    (( |||--)) (_ --_   |    ||    |       made by:                 ", 0xFF0000);
    kklog_color(" ((  ||    ||   || || ||_.   /-|| || || || \\\\  ||      (( |||--))   --_ )  |====[]====|       David Zapletal           ", 0xFF0000);
    kklog_color("  ( / |    ||   || ||  ~ || (( || || || ||/    ||       ( / |  )   _/  ))  |    ||    |                                ", 0xFF0000);
    kklog_color("   \\____-  \\\\,  \\\\/\\\\ ,-_-   \\/\\\\  \\\\/  \\\\,/   \\\\,       -____-   (_-_-         ||                                     ", 0xFF0000);
    kklog_color("                                                                              ------                                   ", 0xFF0000);
    kklog_color("                                DEUS VULT                                                                              ", 0xFF0000);
    kklog_color("                                                                                                                       ", 0xFF0000);
    kklog_color("                          Made by github:Daja201                                                                       ", 0xFF0000);
    kklog_color("                                                                                                                       ", 0xFF0000);
}

void klog_status(const char *status_str, uint32_t color) {
    klog("\n");
    klog_color("sysmsg: ", color);
    kklog_color(status_str, color);
}