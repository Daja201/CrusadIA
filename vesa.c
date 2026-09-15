//standartized VESA graphics driver, set to FullHD 1080p b default, can be changed in loader.s

#include "vesa.h"
#include "font.h"
#include <stdint.h>
#include "string.h"
#include "usbhid.h"

int c_x = 0;
int c_y = 0;
int sc_x = 0;
int sc_y = 0;
int vesa_ready = 0;
int screen_app_mode = 0;

static volatile uint8_t *lfb = 0;
static uint8_t *back = 0;
static uint32_t fb_width = 0;
static uint32_t fb_height = 0;
static uint32_t fb_bpp = 0;
static uint32_t fb_pitch = 0;
static uint32_t mouse_under[5][5];
static int mouse_prev_x = -1, mouse_prev_y = -1;
static int mouse_has_saved = 0;

// reports screen size from bootloader
uint32_t Wwidth(void)  { return fb_width; }
uint32_t Hheight(void) { return fb_height; }

// initializes memory part for vesa buffers
void vesa_init_from_params(uint32_t phys_addr, uint32_t width, uint32_t height, uint32_t bpp, uint32_t pitch) {
    if (width == 0 || height == 0 || width > 8192 || height > 8192) {
        width = 1920; height = 1080; bpp = 32; pitch = width * 4;
    }
    lfb = (volatile uint8_t*)(uintptr_t)phys_addr;
    fb_width = width;
    fb_height = height;
    fb_bpp = bpp;
    fb_pitch = pitch ? pitch : (width * ((bpp + 7) / 8));
    back = (uint8_t *)0x800000;
    pmm_deinit_region(0x800000, fb_pitch * fb_height);
    vesa_ready = 1;
}

// 
static void *vesa_memmove(void *dst, const void *src, uint32_t n) {
    uint8_t *d = (uint8_t*)dst;
    const uint8_t *s = (const uint8_t*)src;
    if (d == s || n == 0) return dst;
    if (d < s) {
        for (uint32_t i = 0; i < n; i++) d[i] = s[i];
    } else {
        for (uint32_t i = n; i > 0; i--) d[i - 1] = s[i - 1];
    }
    return dst;
}

// puts pixel on coordinates with 32bit colour
static inline void put_pixel_32(int x, int y, uint32_t color) {
    if (x < 0 || (uint32_t)x >= fb_width || y < 0 || (uint32_t)y >= fb_height) return;
    uint32_t offset = (y * fb_pitch) + (x * (fb_bpp / 8));
    if (offset >= (fb_pitch * fb_height)) return;
    uint32_t *p = (uint32_t*)(back + offset);
    *p = color;
}

// 
void vesa_draw_char_34(char c, int x, int y, uint32_t fg_color, uint32_t bg_color) {
    if (!vesa_ready) return;
    if (c < 0 || c > 127) return;

    if (x < 0 || y < 0 || (uint32_t)(x + 8) > fb_width || (uint32_t)(y + 8) > fb_height) {
        const unsigned char *glyph_s = font8x8_basic[(int)c];
        for (int cy = 0; cy < 8; cy++) {
            unsigned char row = glyph_s[cy];
            for (int cx = 0; cx < 8; cx++) {
                vesa_putpixel(x + cx, y + cy, (row & (1 << cx)) ? fg_color : bg_color);
            }
        }
        return;
    }

    const unsigned char *glyph = font8x8_basic[(int)c];

    if (fb_bpp == 32) {
        for (int cy = 0; cy < 8; cy++) {
            unsigned char row = glyph[cy];
            uint32_t *p = (uint32_t*)(back + (y + cy) * fb_pitch + x * 4);
            for (int cx = 0; cx < 8; cx++) {
                p[cx] = (row & (1 << cx)) ? fg_color : bg_color;
            }
        }
    } else if (fb_bpp == 24) {
        for (int cy = 0; cy < 8; cy++) {
            unsigned char row = glyph[cy];
            uint8_t *p = back + (y + cy) * fb_pitch + x * 4;
            for (int cx = 0; cx < 8; cx++) {
                uint32_t col = (row & (1 << cx)) ? fg_color : bg_color;
                p[cx * 4 + 0] = col & 0xFF;
                p[cx * 4 + 1] = (col >> 8) & 0xFF;
                p[cx * 4 + 2] = (col >> 16) & 0xFF;
            }
        }
    } else if (fb_bpp == 8) {
        for (int cy = 0; cy < 8; cy++) {
            unsigned char row = glyph[cy];
            for (int cx = 0; cx < 8; cx++) {
                lfb[(y + cy) * fb_pitch + (x + cx)] = (uint8_t)((row & (1 << cx)) ? fg_color : bg_color);
            }
        }
    }
}

void vesa_putpixel_34(int x, int y, uint32_t color) {
    vesa_putpixel(x, y, color);
}

void vesa_putpixel(int x, int y, uint32_t color) {
    if (!vesa_ready) return;
    if (x < 0 || (uint32_t)x >= fb_width || y < 0 || (uint32_t)y >= fb_height) return;
    if (fb_bpp == 32) put_pixel_32(x,y,color);
    else if (fb_bpp == 24) {
        uint8_t *p = (uint8_t*) ( (uintptr_t)back + (uintptr_t)y * fb_pitch + (uintptr_t)x * 4 );
        p[0] = color & 0xFF;
        p[1] = (color >> 8) & 0xFF;
        p[2] = (color >> 16) & 0xFF;
    } else if (fb_bpp == 8) {
        lfb[y * fb_pitch + x] = (uint8_t)color;
    }
}

//whole screen to color
void vesa_clear(uint32_t color) {
    if (!vesa_ready) return;
    for (uint32_t y = 0; y < fb_height; y++) {
        for (uint32_t x = 0; x < fb_width; x++) {
            vesa_putpixel(x, y, color);
        }
    }
    c_x = 0;
    c_y = 0;
}

//swap framebuffer for backbuffer
void vesa_swap(void) {
    if (!vesa_ready || !back) return;
    memcpy((void*)lfb, (void*)back, fb_height * fb_pitch);
}

// scrolls with framebuffer history
void vesa_scroll(int lines) {
    if (!vesa_ready) return;
    if (lines <= 0) return;
    if ((uint32_t)lines >= fb_height) {
        vesa_clear(0x000000);
        return;
    }
    uint32_t bytes = (uint32_t)lines * fb_pitch;
    uint32_t remaining = (fb_height - (uint32_t)lines) * fb_pitch;

    if (fb_bpp == 8) {
        vesa_memmove((void*)lfb, (void*)(lfb + bytes), remaining);
        memset((void*)(lfb + remaining), 0, bytes);
    } else {
        vesa_memmove(back, back + bytes, remaining);
        memset(back + remaining, 0, bytes);
        vesa_swap();
    }
}

//wrapper
void vesa_draw_char(char c, int x, int y, uint32_t fg_color, uint32_t bg_color) {
    vesa_draw_char_34(c, x, y, fg_color, bg_color);
}


void vesa_print_char(char c) {
    if (!vesa_ready) return;
    if (c == '\n') {
        c_x = 0;
        c_y += 8;
        return;
    }
    if (c == '\b') {
        if (c_x >= 8) {
            c_x -= 8;
            vesa_draw_rec(c_x, c_y, 8, 8, 0x000000);
        } else if (c_y >= 8) {
            c_y -= 8;
            c_x = (int)fb_width - 8;
            vesa_draw_rec(c_x, c_y, 8, 8, 0x000000);
        }
        return;
    }
    if (c == '\r') return;

    if (c_x + 8 > (int)fb_width) {
        c_x = 0;
        c_y += 8;
    }
    vesa_draw_char(c, c_x, c_y, 0xFFFFFF, 0x000000);
    c_x += 8;
}

void vesa_print_string(const char *str) {
    if (!str) return;
    while (*str) {
        vesa_print_char(*str++);
    }
}

void vesa_draw_hor(int x, int y, int a, uint32_t col ) {
    for(int b = 0; b < a; b ++) {
        vesa_putpixel(x + b, y, col);
    }
}

void vesa_draw_ver(int x, int y, int a, uint32_t col ) {
    for(int b = 0; b < a; b ++) {
        vesa_putpixel(x , y + b, col);
    }
}

void vesa_draw_rec(int x, int y, int width, int height, uint32_t col ) {
    for(int d = 0; d < height; d ++) {
        for(int c = 0; c < width; c ++) {
            vesa_putpixel(x + c, y + d, col);
        }
    }
}


static uint32_t vesa_get_backpixel(int x, int y) {
    if (x < 0 || (uint32_t)x >= fb_width || y < 0 || (uint32_t)y >= fb_height) return 0;
    if (fb_bpp == 32) {
        return *(uint32_t*)(back + y * fb_pitch + x * 4);
    } else if (fb_bpp == 24) {
        uint8_t *p = back + y * fb_pitch + x * 4;
        return p[0] | (p[1] << 8) | (p[2] << 16);
    }
    return 0;
}

void mouse_draw() {
    if (!vesa_ready) return;
    if (mouse_has_saved) {
        for (int dy = 0; dy < 5; dy++)
            for (int dx = 0; dx < 5; dx++)
                vesa_putpixel(mouse_prev_x + dx, mouse_prev_y + dy, mouse_under[dy][dx]);
    }
    for (int dy = 0; dy < 5; dy++)
        for (int dx = 0; dx < 5; dx++)
            mouse_under[dy][dx] = vesa_get_backpixel(mouse_x + dx, mouse_y + dy);

    vesa_draw_rec(mouse_x, mouse_y, 5, 5, 0xFFFFFF);

    mouse_prev_x = mouse_x;
    mouse_prev_y = mouse_y;
    mouse_has_saved = 1;
}

void screen_appmode() {
    int screen_app_mode = 1;
}

// draws clock on top right
clock_draw() {
    if (!vesa_ready) return;
    int year, month, day, hour, min, sec;
    rtc_get_datetime(&year, &month, &day, &hour, &min, &sec);
    char b[8];
    sc_x = 1800;
    sc_y = 9;
    itoa(hour, b, 10);
    klogol(b, 0x009000);
    klogol(":", 0x009000);
    itoa(min, b, 10);
    klogol(b, 0x009000);
    klogol(":", 0x009000);
    itoa(sec, b, 10);
    klogol(b, 0x009000);
}