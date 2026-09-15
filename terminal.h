#ifndef TERMINAL_H
#define TERMINAL_H
#define CMD_BUF_SIZE 32768
extern char cmd_buf[CMD_BUF_SIZE];
extern int cmd_len;
extern int sac_x;
extern int sac_y;
void terminal_init(void);
void terminal_key(char c);
void klog(const char* s);
void vga_init(void);
#endif
