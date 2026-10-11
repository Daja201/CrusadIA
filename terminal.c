#include "terminal.h"
#include "commands.h"
#include "klog.h"
#include "string.h"
#include "vesa.h"
#include "app.h"

#define HISTORY_SIZE 16

char cmd_buf[CMD_BUF_SIZE];
int cmd_len = 0;
char g_current_path[64] = "";

int sac_x = 0;
int sac_y = 0;

static int cmd_pos = 0;

static char cmd_history[HISTORY_SIZE][CMD_BUF_SIZE];
static int history_count = 0;
static int history_pos = 0;

static void screen_pos_for(int pos, int* out_x, int* out_y) {
    int cells = (int)Wwidth() / 8;
    int col0 = sac_x / 8;
    int total = col0 + pos;
    int row = total / cells;
    int col = total % cells;
    *out_x = col * 8;
    *out_y = sac_y + row * 8;
}

static void move_cursor_to(int pos) {
    cursor('e');
    screen_pos_for(pos, &c_x, &c_y);
    cursor('d');
}

static void redraw_from(int from_pos, int old_len) {
    move_cursor_to(from_pos);
    for (int i = from_pos; i < cmd_len; i++) {
        char s[2] = { cmd_buf[i], 0 };
        klog(s);
    }
    for (int i = cmd_len; i < old_len; i++) {
        klog(" ");
    }
    move_cursor_to(cmd_pos);
}

void execute_command(char* line) {
    char* argv[8];
    int argc = 0;
    char* p = line;
    if (line == NULL || line[0] == '\0') {
        return;
    }
    while (*p && argc < 8) {
        while (*p == ' ') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = 0;
    }
    if (argc == 0) {
        return;
    }
    for (int i = 0; i < command_count; i++) { 
        if (strcmp(argv[0], commands[i].name) == 0) {
            commands[i].func(argc, argv);
            return;
        }
    }
    klog("sorry buddy we don't know this one\n");
}

static void history_add(const char* cmd) {
    if (cmd[0] == '\0') return;
    if (history_count > 0 && strcmp(cmd_history[history_count - 1], cmd) == 0) {
        history_pos = history_count;
        return;
    }
    if (history_count == HISTORY_SIZE) {
        for (int i = 1; i < HISTORY_SIZE; i++) {
            strcpy(cmd_history[i - 1], cmd_history[i]);
        }
        history_count--;
    }
    strcpy(cmd_history[history_count], cmd);
    history_count++;
    history_pos = history_count;
}

static void clear_current_line(void) {
    move_cursor_to(cmd_len);
    while (cmd_len > 0) {
        cmd_len--;
        klog("\b");
    }
    cmd_pos = 0;
}

static void history_recall(int idx) {
    clear_current_line();
    if (idx >= 0 && idx < history_count) {
        strcpy(cmd_buf, cmd_history[idx]);
        cmd_len = strlen(cmd_buf);
        klog(cmd_buf);
        cmd_pos = cmd_len;
    }
}

void terminal_show_prompt(void) {
    klog_color("CRUSADER", 0xFFFF00);
    klog_color(g_current_path, 0xFFA500);
    klog_color(">> ", 0xFFFF00);
    sac_x = c_x;
    sac_y = c_y;
    cmd_pos = 0;
}

void terminal_key(char c) {

    if (screen_app_mode != 0) {
        scriber_key(c);
        return;
    }

    if (c == 0) return;

    if (cmd_len == 0) {
        sac_x = c_x;
        sac_y = c_y;
    }

    if (c == ARROW_UP) {
        if (history_count == 0) return;
        if (history_pos > 0) history_pos--;
        history_recall(history_pos);
        return;
    }

    if (c == ARROW_DOWN) {
        if (history_count == 0) return;
        if (history_pos < history_count - 1) {
            history_pos++;
            history_recall(history_pos);
        } else {
            history_pos = history_count;
            clear_current_line();
        }
        return;
    }

    if (c == ARROW_ESC) {
        return;
    }

    if (c == ARROW_LEFT) {
        if (cmd_pos > 0) {
            cmd_pos--;
            move_cursor_to(cmd_pos);
        }
        return;
    }

    if (c == ARROW_RIGHT) {
        if (cmd_pos < cmd_len) {
            cmd_pos++;
            move_cursor_to(cmd_pos);
        }
        return;
    }

    if (c == '\n') {
        cmd_buf[cmd_len] = 0;
        klog("\n");
        history_add(cmd_buf);
        execute_command(cmd_buf);
        cmd_len = 0;
        cmd_pos = 0;
        if (screen_app_mode == 0) {
            terminal_show_prompt();
        }
        return;
    }

    if (c == '\b') {
        if (cmd_pos > 0) {
            int old_len = cmd_len;
            int pos = cmd_pos;
            for (int i = pos - 1; i < old_len - 1; i++) cmd_buf[i] = cmd_buf[i + 1];
            cmd_len = old_len - 1;
            cmd_pos = pos - 1;
            redraw_from(cmd_pos, old_len);
        }
        return;
    }

    if (cmd_len < CMD_BUF_SIZE - 1) {
        int old_len = cmd_len;
        int pos = cmd_pos;
        for (int i = old_len; i > pos; i--) cmd_buf[i] = cmd_buf[i - 1];
        cmd_buf[pos] = c;
        cmd_len = old_len + 1;
        cmd_pos = pos + 1;
        redraw_from(pos, old_len);
    }
}