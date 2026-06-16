#include "buttons.h"

#include <stdio.h>

#include "main.h"

void buttons_poll(void)
{
  /* 0xFF forces a log on the first poll, whatever the buttons read. */
  static uint8_t fn_prev = 0xFF;

  uint8_t fn = 0;
  if (HAL_GPIO_ReadPin(SW_FN0_BOOT0_GPIO_Port, SW_FN0_BOOT0_Pin) == GPIO_PIN_RESET) fn |= 1;
  if (HAL_GPIO_ReadPin(SW_FN1_GPIO_Port,       SW_FN1_Pin)        == GPIO_PIN_RESET) fn |= 2;
  if (HAL_GPIO_ReadPin(SW_FN2_GPIO_Port,       SW_FN2_Pin)        == GPIO_PIN_RESET) fn |= 4;
  if (fn != fn_prev)
  {
    fn_prev = fn;
    printf("FN: %c%c%c\r\n",
           (fn & 1) ? '1' : '0',
           (fn & 2) ? '1' : '0',
           (fn & 4) ? '1' : '0');
  }
}
