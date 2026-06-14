#include "console.h"
#include <stdio.h>

microrl_t console_rl;

static UART_HandleTypeDef *console_uart;
static uint8_t console_rx_char;

/* >0 while microrl (or our own redraw) is rendering the line; output produced
 * in that window is the prompt/echo itself and must not mark the line dirty. */
static volatile int console_internal_depth;
/* Set when output is written outside microrl rendering (i.e. async app output). */
static volatile uint8_t console_dirty;

/* Routes printf to console_uart. Callers use explicit \r\n. */
int _write(int fd, char *buf, int len)
{
  (void)fd;
  if (!console_uart) return len;
  /* First async write since the prompt was drawn: wipe the prompt line so the
   * message starts at column 0 instead of right after "IRin > ". The prompt is
   * reprinted by console_redraw_prompt() at the bottom of the main loop. */
  if (console_internal_depth == 0 && !console_dirty)
  {
    HAL_UART_Transmit(console_uart, (uint8_t *)"\r\033[K", 4, HAL_MAX_DELAY);
  }
  HAL_UART_Transmit(console_uart, (uint8_t *)buf, len, HAL_MAX_DELAY);
  if (console_internal_depth == 0) console_dirty = 1;
  return len;
}

static void console_print(const char *str)
{
  printf("%s", str);
  fflush(stdout);
}

extern int console_execute(int argc, const char * const *argv);

/* Optional tab-completion provider. If the application defines it, microrl uses
 * it; if not, the weak symbol resolves to NULL and completion is simply off. */
extern char ** console_complete(int argc, const char * const *argv) __attribute__((weak));

void console_rx_callback(uint8_t ch)
{
  /* microrl echoes the char and may redraw the line; that output is not async. */
  console_internal_depth++;
  microrl_insert_char(&console_rl, ch);
  console_internal_depth--;
  HAL_UART_Receive_IT(console_uart, &console_rx_char, 1);
}

int console_take_dirty(void)
{
  __disable_irq();
  int d = console_dirty;
  console_dirty = 0;
  __enable_irq();
  return d;
}

void console_redraw_prompt(void)
{
  /* Cursor is at column 0 of a fresh line (async prints end with \r\n). */
  console_internal_depth++;
  printf("\033[K");                      /* clear to end of line */
  console_print(console_rl.prompt_str);  /* colored prompt */
  for (int i = 0; i < console_rl.cmdlen; i++) {
    char c = console_rl.cmdline[i] ? console_rl.cmdline[i] : ' ';
    putchar(c);
  }
  int back = console_rl.cmdlen - console_rl.cursor;
  if (back > 0) printf("\033[%dD", back);  /* restore cursor position */
  fflush(stdout);
  console_internal_depth--;
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart == console_uart) {
    console_rx_callback(console_rx_char);
  }
}

void console_init(UART_HandleTypeDef *huart, IRQn_Type irqn)
{
  console_uart = huart;

  printf("\r\n=== Bandoneo Console ===\r\n");
  fflush(stdout);

  if (!NVIC_GetEnableIRQ(irqn)) {
    printf("WARNING: IRQ %d not enabled in NVIC, console RX will not work.\r\n"
           "Enable the UART global interrupt in the .ioc NVIC settings and regenerate code.\r\n",
           (int)irqn);
    fflush(stdout);
  }

  microrl_init(&console_rl, console_print);
  microrl_set_execute_callback(&console_rl, console_execute);
  microrl_set_complete_callback(&console_rl, console_complete);

  HAL_UART_Receive_IT(console_uart, &console_rx_char, 1);
}
