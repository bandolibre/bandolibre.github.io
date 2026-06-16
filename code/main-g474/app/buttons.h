#ifndef APP_BUTTONS_H
#define APP_BUTTONS_H

#include <stdbool.h>
#include <stdint.h>

/* Three function buttons (SW_FN0..2) sit on the main board. SW_FN0 shares the
 * BOOT0 pin. Each is active-low (pressed reads GPIO_PIN_RESET). This module
 * polls all three and logs the combined state when it changes.
 *
 * FN0 (left) is a press-to-toggle for "table mode": with the instrument resting
 * on a table (bellows not held), keys still sound. See buttons_table_mode().
 *
 * FN1 (middle) cycles bellows sensitivity through three levels. See
 * buttons_bellow_sens_level(). */

/* Reads the three function buttons and logs the combined state on any change.
 * Call once per main loop iteration. */
void buttons_poll(void);

/* True while table mode is engaged (toggled by each press of FN0). */
bool buttons_table_mode(void);

/* Bellows sensitivity level, 0..2, advanced by each press of FN1. Level 0 is
 * unity scale; levels 1 and 2 apply the bellow_scale_mid / bellow_scale_high
 * properties. */
uint8_t buttons_bellow_sens_level(void);

#endif /* APP_BUTTONS_H */
