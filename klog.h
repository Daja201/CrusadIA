#ifndef KLOG_H
#define KLOG_H
#include "vesa.h"
#include <stdint.h>
void klog(const char *msg);
void kklog(const char *msg);
void klog_hex(uint32_t val);
void klogf(const char *fmt, ...);
void kklogf(const char *fmt, ...);
void logo(void);
void klog_color(const char* msg, uint32_t color);
void kklog_color(const char* msg, uint32_t color);
void klogf_color(const char *fmt, uint32_t color, ...);
void kklogf_color(const char *fmt, uint32_t color, ...);
void appname(const char* name);
void verse(void);
void clock(void);
void free_ram(void);
void new_note(const char* name, const char* content);
void delete_note(const char* name);
void cursor(char func);
void gui(void);
void delete_all_notes(void);
void klog_status(const char *status_str, uint32_t color);
void save_notes_to_disk(void);
void load_notes_from_disk(void);
#endif