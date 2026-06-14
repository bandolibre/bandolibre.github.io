// UART-backed interactive console with printf routing.
// Call console_init() once after UART is ready.
// Implement console_execute() in your application to handle commands.
#ifndef CONSOLE_H
#define CONSOLE_H

#include "microrl.h"
#include "stm32g4xx_hal.h"

extern microrl_t console_rl;

// Set up microrl, route printf to huart, print banner, arm UART RX interrupt.
void console_init(UART_HandleTypeDef *huart, IRQn_Type irqn);

// Feed one received byte into microrl; re-arms the UART interrupt.
void console_rx_callback(uint8_t ch);

// Read-and-clear the "async output happened" flag. Returns non-zero if any
// output was written outside microrl's own line rendering since the last call.
int console_take_dirty(void);

// Reprint the prompt and current input line, restoring the cursor. Assumes the
// cursor is at column 0 of a fresh line (async prints end with \r\n). Pair with
// console_take_dirty() at the bottom of the main loop to restore the prompt
// after asynchronous output.
void console_redraw_prompt(void);

#endif
