//simple engine to run "apps as named sequences of commands or something more"
#include "klog.h"
#include "app.h"
#include "string.h"

void app(const char *app_name) {
    if (strcmp(app_name, "game") == 0) {
        app_game();
    } 
    else {
        kklog("Srr bro no app with this name");
    }
}

void app_game() {
    kklog("NO GAMES YET SRR ):");
}