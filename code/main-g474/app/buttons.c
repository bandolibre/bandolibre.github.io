#include "buttons.h"

#include <stdio.h>

#include "main.h"

/* Latched by buttons_poll() on each FN0 press; read via buttons_table_mode(). */
static bool table_mode = false;

/* Number of bellows sensitivity levels FN1 cycles through. */
#define BELLOW_SENS_LEVELS 3

/* Current bellows sensitivity level (0..BELLOW_SENS_LEVELS-1), advanced by each
 * FN1 press; read via buttons_bellow_sens_level(). */
static uint8_t bellow_sens_level = 0;

/* Latched by buttons_poll() on each FN2 press; read via buttons_bellow_inertia(). */
static bool bellow_inertia = false;

bool buttons_table_mode(void)
{
  return table_mode;
}

uint8_t buttons_bellow_sens_level(void)
{
  return bellow_sens_level;
}

bool buttons_bellow_inertia(void)
{
  return bellow_inertia;
}

void buttons_poll(void)
{
  /* 0xFF forces a log on the first poll, whatever the buttons read. Its FN0 bit
   * is set, so a button already held at boot is not seen as a fresh press. */
  static uint8_t fn_prev = 0xFF;

  uint8_t fn = 0;
  /* SW_FN0 shares BOOT0, which carries the boot-mode pulldown, so it reads the
   * opposite way from the other two: low while idle, high (SET) when pressed.
   * Match against SET so a press sets the bit like the active-low FN1/FN2. */
  if (HAL_GPIO_ReadPin(SW_FN0_BOOT0_GPIO_Port, SW_FN0_BOOT0_Pin) == GPIO_PIN_SET) fn |= 1;
  if (HAL_GPIO_ReadPin(SW_FN1_GPIO_Port,       SW_FN1_Pin)        == GPIO_PIN_RESET) fn |= 2;
  if (HAL_GPIO_ReadPin(SW_FN2_GPIO_Port,       SW_FN2_Pin)        == GPIO_PIN_RESET) fn |= 4;

  /* Act on rising edges (press, not release). FN0 toggles table mode; FN1
   * advances the bellows sensitivity level, wrapping after the last; FN2
   * toggles the bellows inertia mode. */
  if ((fn & 1) && !(fn_prev & 1)) table_mode = !table_mode;
  if ((fn & 2) && !(fn_prev & 2)) bellow_sens_level = (bellow_sens_level + 1) % BELLOW_SENS_LEVELS;
  if ((fn & 4) && !(fn_prev & 4)) bellow_inertia = !bellow_inertia;

  if (fn != fn_prev)
  {
    fn_prev = fn;
    printf("FN: %c%c%c  table_mode=%u  bellow_sens_level=%u  bellow_inertia=%u\r\n",
           (fn & 1) ? '1' : '0',
           (fn & 2) ? '1' : '0',
           (fn & 4) ? '1' : '0',
           table_mode, bellow_sens_level, bellow_inertia);
  }
}
