#pragma once

#include <stdarg.h>

/* Send a string over ITM port 0. No-op when no tracer is active. */
void swo_print(const char *s);

/* Format and send over ITM port 0 (64-byte stack buffer). */
void swo_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
