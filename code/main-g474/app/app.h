#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// DWT cycle counter, console, USB, and keyboard — call once after MX_*_Init().
void main_init(void);

// One main-loop iteration: poll all subsystems and drain the console.
void main_task(void);

#ifdef __cplusplus
}
#endif
