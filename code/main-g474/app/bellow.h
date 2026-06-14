#ifndef APP_BELLOW_H
#define APP_BELLOW_H

#include <stdbool.h>
#include <stdint.h>
#include "keyboard_layout.h"

/* The bandoneon is bisonoric: each key sounds a different note on push vs pull.
 * This module reads the two hall sensors, tracks the bellows direction and how
 * hard it is being pushed or pulled (both derived from the combined hall
 * reading), and emits CC#11 (Expression) from the intensity. */

/* Current bellows direction (BELLOWS_NEUTRAL/PUSH/PULL). */
bellows_t bellow_direction(void);

/* Samples both hall sensors, updates the direction/intensity, and emits the
 * expression CC. Returns true if the direction changed this call (so the caller
 * can re-evaluate sounding notes). Call once per main loop iteration. */
bool bellow_poll(void);

#endif /* APP_BELLOW_H */
