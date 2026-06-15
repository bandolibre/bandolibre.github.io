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
 * expression CC. Call once per main loop iteration. Read the result via
 * bellow_direction(); consumers track changes themselves. */
void bellow_poll(void);

/* Diagnostic sweep over a range of the bellow_settle_us property: for each
 * value, repeatedly samples both hall sensors and prints a table of their mean
 * and standard deviation, to pick the smallest settling delay that reads
 * stably. Blocks the main loop for a few seconds and restores bellow_settle_us
 * before returning. Intended for the console 'bellow_tune' command. */
void bellow_tune(void);

#endif /* APP_BELLOW_H */
