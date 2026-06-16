#include "buttons.h"

#include <stdio.h>

#include "main.h"

/* Latched by buttons_poll() on each FN0 press; read via buttons_table_mode(). */
static bool table_mode = false;

bool buttons_table_mode(void)
{
  return table_mode;
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

  /* Toggle table mode on the rising edge of FN0 (press, not release). */
  if ((fn & 1) && !(fn_prev & 1)) table_mode = !table_mode;

  if (fn != fn_prev)
  {
    fn_prev = fn;
    printf("FN: %c%c%c  table_mode=%u\r\n",
           (fn & 1) ? '1' : '0',
           (fn & 2) ? '1' : '0',
           (fn & 4) ? '1' : '0',
           table_mode);
  }
}
