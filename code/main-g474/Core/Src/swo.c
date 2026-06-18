#include "swo.h"

#include <stdint.h>
#include <stdio.h>
#include "main.h"

void swo_print(const char *s)
{
  while (*s)
  {
    if ((ITM->TCR & ITM_TCR_ITMENA_Msk) && (ITM->TER & (1UL << 0)))
    {
      while (ITM->PORT[0].u32 == 0);
      ITM->PORT[0].u8 = (uint8_t)*s;
    }
    s++;
  }
}

void swo_printf(const char *fmt, ...)
{
  char buf[128];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  swo_print(buf);
}
