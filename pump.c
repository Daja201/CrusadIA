//DRIVER FOR COMMUNICATION WITH EDWARDS
// *NEXT 400D*
//TURBOMOLECULAR PUMP
// SHOULD WORK

#include "pump.h"
#include "pl2303.h"
#include "usb.h"
#include "klog.h"
#include "string.h"

#define PUMP_READ_RETRIES 40

int pump_is_ready(void) {
    return pl2303_get_active() != 0;
}

int pump_send_command(char start_char, const char* body, char* response, int response_maxlen) {
    usb_device_t* dev = pl2303_get_active();
    if (!dev) {
        klog_color("pump: no USB-serial adapter attached\n", 0xFF0000);
        return -1;
    }

    char msg[PUMP_MAX_MSG];
    int len = 0;
    msg[len++] = start_char;
    int blen = (int)strlen(body);
    if (blen > (int)sizeof(msg) - 3) blen = (int)sizeof(msg) - 3;
    memcpy(msg + len, body, blen);
    len += blen;
    msg[len++] = '\r';

    if (usb_bulk_write(dev, msg, len) < 0) {
        klog_color("pump: write failed\n", 0xFF0000);
        return -1;
    }

    if (!response || response_maxlen <= 0) return 0;

    int total = 0;
    for (int attempt = 0; attempt < PUMP_READ_RETRIES && total < response_maxlen - 1; attempt++) {
        uint8_t chunk[64];
        int r = usb_bulk_read(dev, chunk, sizeof(chunk));
        if (r <= 0) continue;
        for (int i = 0; i < r && total < response_maxlen - 1; i++) {
            if (chunk[i] == '\r') {
                response[total] = 0;
                return total;
            }
            response[total++] = (char)chunk[i];
        }
    }
    response[total] = 0;
    return total > 0 ? total : -1;
}

int pump_start(char* response, int response_maxlen) {
    return pump_send_command('!', "C852 1", response, response_maxlen);
}

int pump_stop(char* response, int response_maxlen) {
    return pump_send_command('!', "C852 0", response, response_maxlen);
}

int pump_query_speed(char* response, int response_maxlen) {
    return pump_send_command('?', "V852", response, response_maxlen);
}

int pump_query_status(char* response, int response_maxlen) {
    return pump_send_command('?', "V852", response, response_maxlen);
}

int pump_target_full_speed(char* response, int response_maxlen) {
    return pump_send_command('!', "C869 0", response, response_maxlen);
}

int pump_target_standby_speed(char* response, int response_maxlen) {
    return pump_send_command('!', "C869 1", response, response_maxlen);
}

int pump_query_pump_type(char* response, int response_maxlen) {
    return pump_send_command('?', "S851", response, response_maxlen);
}

int pump_close_vent(char* response, int response_maxlen) {
    return pump_send_command('!', "C875 1", response, response_maxlen);
}
