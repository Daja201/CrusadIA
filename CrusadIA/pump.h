#ifndef PUMP_H
#define PUMP_H
#include <stdint.h>

/* Control layer for Edwards nEXT-series turbomolecular pumps (nEXT240D /
 * nEXT400D) over their RS232 logic-interface link, reached through a
 * PL2303 USB-serial adapter (see pl2303.h). Protocol per the pump's
 * manual section 3.7: 9600 baud 8N1, ASCII messages of the form
 *   '!' or '?'  + COMMAND-LETTER + 3-digit object number [+ ' ' data] + CR
 * '!' stores/commands a value, '?' queries one. Every message gets a
 * single-line response terminated by CR.
 *
 * Note: the pump's own "Serial Enable" interlock line (pin 5) must be
 * wired to 0V (pin 2) for it to accept anything over this link - that is
 * a hardware/cabling requirement, this driver can't do it for you.
 */

#define PUMP_MAX_MSG 80

/* Returns 1 if a PL2303 adapter is attached and ready to talk to the pump. */
int pump_is_ready(void);

/* Sends a raw command (without leading '!'/'?' or trailing CR - those are
   added for you) and blocks for the pump's single-line reply. Returns the
   reply length (0 or more), or -1 on timeout / no adapter attached. */
int pump_send_command(char start_char, const char* body, char* response, int response_maxlen);

/* Convenience wrappers around the command set in Table 16 of the manual. */
int pump_start(char* response, int response_maxlen);          /* !C852 1 */
int pump_stop(char* response, int response_maxlen);            /* !C852 0 */
int pump_query_speed(char* response, int response_maxlen);     /* ?V852   */
int pump_query_status(char* response, int response_maxlen);    /* ?V852 (status word half of same object) */
int pump_target_full_speed(char* response, int response_maxlen);    /* !C869 0 */
int pump_target_standby_speed(char* response, int response_maxlen); /* !C869 1 */
int pump_query_pump_type(char* response, int response_maxlen); /* ?S851 */
int pump_close_vent(char* response, int response_maxlen);      /* !C875 1 */

#endif
