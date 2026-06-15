#ifndef APP_PEDALS_H
#define APP_PEDALS_H

/* Two external pedals plug into the main board: an expression pedal and a
 * sustain pedal. Each has a presence-detect line (EXP/SUS_PEDAL_INT, high when a
 * pedal is plugged in) and an analog wiper read by ADC1 (expression) / ADC2
 * (sustain). This module polls both and logs presence changes and wiper
 * movement. */

/* Reads both pedals' presence lines and wiper ADCs, and logs presence changes
 * and wiper movement past a small threshold. Call once per main loop
 * iteration. */
void pedals_poll(void);

#endif /* APP_PEDALS_H */
