#include "terminal.h"
#include "commands.h"
#include "klog.h"
#include <string.h>

#define HISTORY_SIZE 16
#define ARROW_UP 1
#define ARROW_DOWN 2

char cmd_buf[CMD_BUF_SIZE];
int cmd_len = 0;
char g_current_path[64] = "";

static char cmd_history[HISTORY_SIZE][CMD_BUF_SIZE];
static int history_count = 0;
static int history_pos = 0;

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
    while (cmd_len > 0) {
        cmd_len--;
        klog("\b");
    }
}

static void history_recall(int idx) {
    clear_current_line();
    if (idx >= 0 && idx < history_count) {
        strcpy(cmd_buf, cmd_history[idx]);
        cmd_len = strlen(cmd_buf);
        klog(cmd_buf);
    }
}

void terminal_key(char c) {
    if (c == 0) return;

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

    if (c == '\n') {
        cmd_buf[cmd_len] = 0;
        klog("\n");
        history_add(cmd_buf);
        execute_command(cmd_buf);
        cmd_len = 0;
        klog_color("CRUSADER", 0xFFFF00);
        klog_color(g_current_path, 0xFFA500);
        klog_color(">> ", 0xFFFF00);
        return;
    }

    if (c == '\b') {
        if (cmd_len > 0) {
            cmd_len--;
            klog("\b");
        }
        return;
    }

    if (cmd_len < CMD_BUF_SIZE - 1) {
        cmd_buf[cmd_len++] = c;
        char str[2] = {c, '\0'};
        klog(str); 
    }
}