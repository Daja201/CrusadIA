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
