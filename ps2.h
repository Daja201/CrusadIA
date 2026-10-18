#ifndef PS2_H
#define PS2_H
#include <stdint.h>

void ps2_init(void);
void ps2_poll(void);
int ps2_keyboard_present(void);
int ps2_mouse_present(void);

#endif
