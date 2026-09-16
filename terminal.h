#ifndef TERMINAL_H
#define TERMINAL_H
#define CMD_BUF_SIZE 32768
#define ARROW_UP 1
#define ARROW_DOWN 2
#define ARROW_ESC 3
#define ARROW_LEFT 4
#define ARROW_RIGHT 5
extern char cmd_buf[CMD_BUF_SIZE];
extern int cmd_len;
extern int sac_x;
extern int sac_y;
void terminal_init(void);
void terminal_key(char c);
void terminal_show_prompt(void);
void klog(const char* s);
void vga_init(void);
#endif