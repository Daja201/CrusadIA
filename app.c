//simple engine to run "apps as named sequences of commands or something more"
#include "klog.h"
#include "app.h"
#include "string.h"
#include "pmm.h"

void app(const char *app_name) {
    if (strcmp(app_name, "scriber") == 0) {
        app_scriber();
    } 
    else {
        kklog("Srr bro no app with this name");
    }
}

void app_scriber() {
    uint32_t addr = pmm_alloc_block(); //allocated space for file
    vesa_clear(0X000000);
    //screen_appmode();
}