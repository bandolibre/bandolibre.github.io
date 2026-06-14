#ifndef APP_BELLOW_H
#define APP_BELLOW_H

#include <stdint.h>
#include "keyboard/keyboard_layout.h"

/* The bandoneon is bisonoric: each key sounds a different note on push vs pull.
 * This module tracks the bellows direction and how hard it is being pushed or
 * pulled, both derived from the combined hall reading (hall0+hall1) by
 * bellow_update(), and emits CC#11 (Expression) from the intensity. */

/* Current bellows direction (BELLOWS_NEUTRAL/PUSH/PULL). */
bellows_t bellow_direction(void);

/* Updates direction/intensity from the combined hall reading.
 * In NEUTRAL, intensity is 0 and direction holds until the reading passes the
 * deadzone edge. In PUSH/PULL, intensity scales linearly from the deadzone
 * edge to the hard push/pull reading (clamped to 1024: 0 is barely moving
 * air, 1024 is full force), and direction holds until the reading comes back
 * past the deadzone edge by bellow_hyst. Call once per main loop iteration. */
void bellow_update(uint32_t hall_total);

/* Sends CC#11 (Expression) from the current intensity, scaled to 0..127, when
 * it has moved by at least bellow_cchyst since the last send (or on the first
 * call) and at most every bellow_ccper ms. Call once per main loop iteration
 * after bellow_update(). */
void bellow_send_cc(void);

#endif /* APP_BELLOW_H */
