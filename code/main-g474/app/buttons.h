#ifndef APP_BUTTONS_H
#define APP_BUTTONS_H

/* Three function buttons (SW_FN0..2) sit on the main board. SW_FN0 shares the
 * BOOT0 pin. Each is active-low (pressed reads GPIO_PIN_RESET). This module
 * polls all three and logs the combined state when it changes. */

/* Reads the three function buttons and logs the combined state on any change.
 * Call once per main loop iteration. */
void buttons_poll(void);

#endif /* APP_BUTTONS_H */
