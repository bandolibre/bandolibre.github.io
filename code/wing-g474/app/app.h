#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Console and SPI diagnostics — call once after MX_*_Init().
void main_init(void);

// One main-loop iteration: scan hall sensors, transmit SPI frame, poll console.
void main_task(void);

#ifdef __cplusplus
}
#endif
