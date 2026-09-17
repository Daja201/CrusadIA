//16550 UART driver for serial communication
/*
COM1: 0x3F8 (IRQ4)
COM2: 0x2F8 (IRQ3)
COM3: 0x3E8 (IRQ4)
COM4: 0x2E8 (IRQ3)
*/


#define COM1 0x3f8 // COM1
#define COM2 0x2F8         
#define COM3 0x3E8
#define COM4 0x2E8
#include "io.h"

//initialize

void init_serial() {
   outb(COM1 + 1, 0x00);
   outb(COM1 + 3, 0x80);
   outb(COM1 + 0, 0x03); 
   outb(COM1 + 1, 0x00); 
   outb(COM1 + 3, 0x03);
   outb(COM1 + 2, 0xC7); 
   outb(COM1 + 4, 0x0B); 
   outb(COM1 + 4, 0x1E); 
   outb(COM1 + 0, 0xAE);
   if(inb(COM1 + 0) != 0xAE) {
      return;
   }
   outb(COM1 + 4, 0x0F);
}

//receive

int serial_received() {
   return inb(COM1 + 5) & 1;
}

char read_serial() {
   while (serial_received() == 0);
   return inb(COM1);
}

//send

int is_transmit_empty() {
   return inb(COM1 + 5) & 0x20;
}

void write_serial(char a) {
   while (is_transmit_empty() == 0);
   outb(COM1,a);
}