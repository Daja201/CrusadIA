#ifndef _GUEST_OS_H
#define _GUEST_OS_H
#include <stdint.h>

void klog(const char *s); 
void kklog(const char *s);  
void klogf(const char *fmt, ...); 
void klog_color(const char *s, uint32_t rgb);

uint32_t Wwidth(void);
uint32_t Hheight(void);
void vesa_putpixel(int x, int y, uint32_t rgb);
void vesa_draw_rec(int x, int y, int w, int h, uint32_t rgb);
void vesa_clear(uint32_t rgb);
void vesa_swap(void);   

extern volatile uint32_t system_ticks;
void os_sleep_ms(uint32_t ms);
void rtc_get_datetime(int *year, int *month, int *day, int *hour, int *min, int *sec);

#define O_RDONLY 0x0000
#define O_WRONLY 0x0001
#define O_RDWR   0x0002
#define O_CREAT  0x0040
#define O_TRUNC  0x0200
#define O_APPEND 0x0400
int  open(const char *path, int flags, ...);
long read(int fd, void *buf, unsigned long n);
long write(int fd, const void *buf, unsigned long n);
long lseek(int fd, long off, int whence);
int  close(int fd);
int  unlink(const char *path);
#endif
