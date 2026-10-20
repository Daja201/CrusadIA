#ifndef COMMANDS_H
#define COMMANDS_H
#include "string.h"
typedef void (*cmd_func_t)(int argc, char** argv);
typedef struct {
    const char* name;
    cmd_func_t func;
} command_t;
extern command_t commands[];
extern int command_count;
extern uint8_t timezone;
void cmd_read(int argc, char** argv);
void cmd_ls(int argc, char** argv);
void cmd_pwd(int argc, char** argv);
void cmd_mkdir(int argc, char** argv);
void cmd_rm(int argc, char** argv);
void cmd_cp(int argc, char** argv);
void cmd_mv(int argc, char** argv);
void cmd_touch(int argc, char** argv);
void cmd_cat(int argc, char** argv);
void cmd_meow(int argc, char** argv);
void cmd_echo(int argc, char** argv);
void cmd_printf(int argc, char** argv);
void cmd_head(int argc, char** argv);
void cmd_tail(int argc, char** argv);
void cmd_wc(int argc, char** argv);
void cmd_grep(int argc, char** argv);
void cmd_sort(int argc, char** argv);
void cmd_uniq(int argc, char** argv);
void cmd_true(int argc, char** argv);
void cmd_false(int argc, char** argv);
void cmd_whoami(int argc, char** argv);
void cmd_uname(int argc, char** argv);
void cmd_basename(int argc, char** argv);
void cmd_dirname(int argc, char** argv);
void cmd_which(int argc, char** argv);
void cmd_find(int argc, char** argv);
void cmd_time(int argc, char** argv);
void drives(void);
void cmd_time(int argc, char** argv);
void cmd_pumpon(int argc, char** argv);
void cmd_pumpoff(int argc, char** argv);
void cmd_pumpspeed(int argc, char** argv);
void cmd_pumpfull(int argc, char** argv);
void cmd_pumpstandby(int argc, char** argv);
void cmd_pumptype(int argc, char** argv);
void cmd_pumpcmd(int argc, char** argv);
void cmd_blink(int argc, char** argv);
#endif