#include "klog.h"
#include "app.h"
#include "string.h"
#include "terminal.h"
#include "vesa.h"
#include "fs.h"

#define SCRIBER_BUF_SIZE 4096
#define SCRIBER_FNAME_SIZE 32
#define SCRIBER_MODE_EDIT 0
#define SCRIBER_MODE_MENU 1
#define SCRIBER_MODE_FNAME 2

static char scriber_buf[SCRIBER_BUF_SIZE];
static int scriber_len = 0;
static int scriber_pos = 0;
static int scriber_mode = SCRIBER_MODE_EDIT;
static char scriber_fname[SCRIBER_FNAME_SIZE];
static int scriber_fname_len = 0;

void app(const char *app_name) {
    if (strcmp(app_name, "scriber") == 0) {
        app_scriber();
    }
    else {
        kklog("Srr bro no app with this name");
    }
}

static int scriber_line_start(int idx) {
    while (idx > 0 && scriber_buf[idx - 1] != '\n') idx--;
    return idx;
}

static int scriber_line_end(int idx) {
    while (idx < scriber_len && scriber_buf[idx] != '\n') idx++;
    return idx;
}

static void scriber_move_up(void) {
    int ls = scriber_line_start(scriber_pos);
    if (ls == 0) return;
    int col = scriber_pos - ls;
    int prev_end = ls - 1;
    int prev_start = scriber_line_start(prev_end);
    int prev_len = prev_end - prev_start;
    int newcol = col < prev_len ? col : prev_len;
    scriber_pos = prev_start + newcol;
}

static void scriber_move_down(void) {
    int ls = scriber_line_start(scriber_pos);
    int le = scriber_line_end(scriber_pos);
    if (le >= scriber_len) return;
    int col = scriber_pos - ls;
    int next_start = le + 1;
    int next_end = scriber_line_end(next_start);
    int next_len = next_end - next_start;
    int newcol = col < next_len ? col : next_len;
    scriber_pos = next_start + newcol;
}

static void scriber_insert(char c) {
    if (scriber_len >= SCRIBER_BUF_SIZE - 1) return;
    for (int i = scriber_len; i > scriber_pos; i--) {
        scriber_buf[i] = scriber_buf[i - 1];
    }
    scriber_buf[scriber_pos] = c;
    scriber_len++;
    scriber_pos++;
}

static void scriber_backspace(void) {
    if (scriber_pos == 0) return;
    for (int i = scriber_pos - 1; i < scriber_len - 1; i++) {
        scriber_buf[i] = scriber_buf[i + 1];
    }
    scriber_len--;
    scriber_pos--;
}

static void scriber_draw_text(const char *s, int x, int y, uint32_t color) {
    while (*s) {
        vesa_draw_char_34(*s, x, y, color, 0x000000);
        x += 8;
        s++;
    }
}

static void scriber_redraw(void) {
    vesa_draw_rec(0, 0, 1920, 1064, 0x000000);
    int x = 1, y = 1;
    int cur_x = 1, cur_y = 1;
    for (int i = 0; i <= scriber_len; i++) {
        if (i == scriber_pos) {
            cur_x = x;
            cur_y = y;
        }
        if (i == scriber_len) break;
        char c = scriber_buf[i];
        if (c == '\n') {
            x = 1;
            y += 8;
            continue;
        }
        vesa_draw_char_34(c, x, y, 0xFFFFFF, 0x000000);
        x += 8;
        if (x > 1920 - 8) {
            x = 1;
            y += 8;
        }
        if (y > 1064 - 8) break;
    }
    vesa_draw_rec(cur_x, cur_y + 1, 3, 7, 0xFFFF00);
}

static void scriber_draw_menu(void) {
    vesa_draw_rec(0, 1064, 1920, 16, 0x202020);
    scriber_draw_text("ESC:resume  s:save  d:discard", 8, 1068, 0xFFFF00);
}

static void scriber_draw_fname(void) {
    vesa_draw_rec(0, 1064, 1920, 16, 0x202020);
    scriber_draw_text("filename (enter to save, esc to cancel): ", 8, 1068, 0xFFFF00);
    char tmp[SCRIBER_FNAME_SIZE + 1];
    for (int i = 0; i < scriber_fname_len; i++) tmp[i] = scriber_fname[i];
    tmp[scriber_fname_len] = 0;
    scriber_draw_text(tmp, 8 + 42 * 8, 1068, 0xFFFFFF);
}

static void scriber_exit(void) {
    scriber_len = 0;
    scriber_pos = 0;
    scriber_fname_len = 0;
    scriber_mode = SCRIBER_MODE_EDIT;
    screen_appmode();
    vesa_clear(0x000000);
    c_x = 0;
    c_y = 0;
    terminal_show_prompt();
}

static void scriber_save(void) {
    if (scriber_fname_len == 0) {
        scriber_mode = SCRIBER_MODE_MENU;
        scriber_draw_menu();
        return;
    }
    scriber_fname[scriber_fname_len] = 0;
    uint32_t inode = fs_create_file(scriber_fname, "scb");
    if ((int32_t)inode >= 0) {
        fs_write(inode, 0, (const uint8_t*)scriber_buf, scriber_len);
    }
    scriber_exit();
}

void app_scriber(void) {
    scriber_len = 0;
    scriber_pos = 0;
    scriber_fname_len = 0;
    scriber_mode = SCRIBER_MODE_EDIT;
    vesa_clear(0x000000);
    screen_appmode();
    scriber_redraw();
}

void scriber_key(char c) {
    if (scriber_mode == SCRIBER_MODE_MENU) {
        if (c == ARROW_ESC) {
            scriber_mode = SCRIBER_MODE_EDIT;
            scriber_redraw();
            return;
        }
        if (c == 's') {
            scriber_mode = SCRIBER_MODE_FNAME;
            scriber_fname_len = 0;
            scriber_draw_fname();
            return;
        }
        if (c == 'd') {
            scriber_exit();
            return;
        }
        return;
    }

    if (scriber_mode == SCRIBER_MODE_FNAME) {
        if (c == ARROW_ESC) {
            scriber_mode = SCRIBER_MODE_MENU;
            scriber_draw_menu();
            return;
        }
        if (c == '\n') {
            scriber_save();
            return;
        }
        if (c == '\b') {
            if (scriber_fname_len > 0) scriber_fname_len--;
            scriber_draw_fname();
            return;
        }
        if (c >= 32 && c < 127 && scriber_fname_len < SCRIBER_FNAME_SIZE - 1) {
            scriber_fname[scriber_fname_len++] = c;
            scriber_draw_fname();
        }
        return;
    }

    if (c == 0) return;

    if (c == ARROW_UP) {
        scriber_move_up();
        scriber_redraw();
        return;
    }

    if (c == ARROW_DOWN) {
        scriber_move_down();
        scriber_redraw();
        return;
    }

    if (c == ARROW_LEFT) {
        if (scriber_pos > 0) scriber_pos--;
        scriber_redraw();
        return;
    }

    if (c == ARROW_RIGHT) {
        if (scriber_pos < scriber_len) scriber_pos++;
        scriber_redraw();
        return;
    }

    if (c == ARROW_ESC) {
        scriber_mode = SCRIBER_MODE_MENU;
        scriber_draw_menu();
        return;
    }

    if (c == '\b') {
        scriber_backspace();
        scriber_redraw();
        return;
    }

    scriber_insert(c);
    scriber_redraw();
}
