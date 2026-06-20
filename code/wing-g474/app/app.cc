#include <stdio.h>
#include <string.h>

extern "C" {
#include "main.h"
#include "console.h"
#include "hall_report.h"
#include "ansi.h"
#include "app/app.h"

extern ADC_HandleTypeDef hadc1;
extern ADC_HandleTypeDef hadc2;
extern ADC_HandleTypeDef hadc3;
extern ADC_HandleTypeDef hadc4;
extern ADC_HandleTypeDef hadc5;
extern SPI_HandleTypeDef hspi1;
extern UART_HandleTypeDef huart1;
}

static uint8_t read_wing_id(void);

#define HALL_NUM_SEL   4
#define HALL_NUM_ADC   5
#define HALL_NUM_RANK  2
// Slots per ADC across a full sweep: one (sel, rank) pair each.
#define HALL_SLOTS_PER_ADC (HALL_NUM_SEL * HALL_NUM_RANK)
#define HALL_DEAD_ZONE 64
// Settling delay after switching the SEL mux, in spin-loop iterations.
#define MUX_LATENCY_CYCLES 160

// Number of hall sensor slots == number of physical keys on this wing.
#define KEYBOARD_NUM_KEYS (HALL_NUM_ADC * HALL_SLOTS_PER_ADC)

// Layout matches the per-ADC DMA buffer: [adc][sel*HALL_NUM_RANK + rank]. This
// is also exactly the flattened key order the SPI frame and hall_report use
// (index = adc * HALL_SLOTS_PER_ADC + slot), so g_hall_data can be passed to
// the shared report as a flat uint16_t[KEYBOARD_NUM_KEYS].
_Static_assert(KEYBOARD_NUM_KEYS == HALL_REPORT_NUM_KEYS,
               "wing key count must match the shared hall report layout");
static uint16_t g_hall_data[HALL_NUM_ADC][HALL_SLOTS_PER_ADC];
static uint16_t g_hall_last_reported[HALL_NUM_ADC][HALL_SLOTS_PER_ADC];

// Running per-key statistics, fed by hall_stats_update() every sweep.
static hall_stats_t g_hall_stats;


// The .ioc only sets the board layer (pins -> analog, DMA1_Ch1..5 -> ADC1..5,
// circular periph->mem halfword, MINC, NVIC). It leaves each ADC in single-
// conversion mode (ScanConvMode disabled, NbrOfConversion=1, DMAContinuousReq
// disabled). We switch each ADC to a 2-rank scan once at startup.

// Switch one ADC to a 2-rank regular scan feeding the (circular) DMA.
static void adc_set_scan2(ADC_HandleTypeDef *hadc, uint32_t chA, uint32_t chB)
{
  HAL_ADC_Stop(hadc);
  hadc->Init.ScanConvMode          = ADC_SCAN_ENABLE;
  hadc->Init.NbrOfConversion       = 2;
  hadc->Init.DMAContinuousRequests = ENABLE;   // match DMA_CIRCULAR from the MSP
  HAL_ADC_Init(hadc);

  ADC_ChannelConfTypeDef c = {0};
  c.SamplingTime = ADC_SAMPLETIME_2CYCLES_5;
  c.SingleDiff   = ADC_SINGLE_ENDED;
  c.OffsetNumber = ADC_OFFSET_NONE;
  c.Rank = ADC_REGULAR_RANK_1; c.Channel = chA; HAL_ADC_ConfigChannel(hadc, &c);
  c.Rank = ADC_REGULAR_RANK_2; c.Channel = chB; HAL_ADC_ConfigChannel(hadc, &c);
}

// Performs one full sweep into hall_data.
static void hall_keyboard_scan(uint16_t hall_data[HALL_NUM_ADC][HALL_SLOTS_PER_ADC])
{
  static int initialized = 0;
  static ADC_HandleTypeDef *const adcs[5] = { &hadc1, &hadc2, &hadc3, &hadc4, &hadc5 };
  static const uint32_t chA[5] = { ADC_CHANNEL_1, ADC_CHANNEL_3, ADC_CHANNEL_12, ADC_CHANNEL_4, ADC_CHANNEL_1 };
  static const uint32_t chB[5] = { ADC_CHANNEL_2, ADC_CHANNEL_4, ADC_CHANNEL_1,  ADC_CHANNEL_5, ADC_CHANNEL_2 };

  if (!initialized)
  {
    HAL_GPIO_WritePin(HALL_NEN_GPIO_Port, HALL_NEN_Pin, GPIO_PIN_RESET);
    HAL_Delay(5);
    for (int a = 0; a < 5; a++)
      adc_set_scan2(adcs[a], chA[a], chB[a]);
    initialized = 1;
  }

  for (int sel = 0; sel < HALL_NUM_SEL; sel++)
  {
    HAL_GPIO_WritePin(SEL0_GPIO_Port, SEL0_Pin, (sel & 1) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(SEL1_GPIO_Port, SEL1_Pin, (sel & 2) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    for (volatile int i = 0; i < MUX_LATENCY_CYCLES; i++) __NOP();

    if (sel == 0)
    {
      // Arm DMA for the whole sweep (8 = 4 sel x 2 ranks) straight into
      // hall_data, whose layout already matches the per-ADC buffer. Start_DMA
      // also issues the software trigger for sel 0. The circular buffer wraps
      // back to index 0 after exactly 8 transfers, i.e. at the end of the sweep.
      for (int a = 0; a < 5; a++)
        HAL_ADC_Start_DMA(adcs[a], (uint32_t *)hall_data[a], HALL_SLOTS_PER_ADC);
    }
    else
    {
      // ADCs are armed; just re-trigger. ContinuousConvMode is disabled, so the
      // ADC halts after each 2-rank sequence -> ADSTART is a clean per-sel gate.
      for (int a = 0; a < 5; a++)
        SET_BIT(adcs[a]->Instance->CR, ADC_CR_ADSTART);
    }

    // Wait for all 5 sequences (2 conversions each) to finish.
    for (int a = 0; a < 5; a++)
      while (READ_BIT(adcs[a]->Instance->CR, ADC_CR_ADSTART)) { }
  }
  for (int a = 0; a < 5; a++)
    HAL_ADC_Stop_DMA(adcs[a]);

  // DMA wrote straight into hall_data in its [adc][sel*rank + rank] layout,
  // so no de-interleave step is needed.
}

// Resets the stddev window every STATS_RESET_PERIOD_MS so stddev reflects only
// the last window; min/max are left untouched (see hall_stats_reset_window).
#define STATS_RESET_PERIOD_MS 3000

// Reports hall_data slots that moved by at least HALL_DEAD_ZONE since the
// last report.
static void report_hall_changes(uint16_t hall_data[HALL_NUM_ADC][HALL_SLOTS_PER_ADC])
{
  for (int a = 0; a < HALL_NUM_ADC; a++)
    for (int s = 0; s < HALL_SLOTS_PER_ADC; s++)
    {
      uint16_t cur  = hall_data[a][s];
      uint16_t last = g_hall_last_reported[a][s];
      int delta = (int)cur - (int)last;
      if (delta < 0) delta = -delta;
      if (last && delta >= HALL_DEAD_ZONE)
      {
        printf(" > dma scan    :  adc=%d sel=%d rank=%d: %4u -> %4u\r\n",
               a, s / HALL_NUM_RANK, s % HALL_NUM_RANK,
               (unsigned)last, (unsigned)cur);
        g_hall_last_reported[a][s] = cur;
      }
    }
}

// Builds and sends one full keyboard frame over the SPI link: word 0 is this
// wing's id, words 1..KEYBOARD_NUM_KEYS are the raw hall measurement for each
// key in flattened order (adc * HALL_SLOTS_PER_ADC + slot). The frame carries
// the full absolute keyboard state every cycle, so a dropped (CRC-failed)
// frame just leaves the receiver at its previous state until the next arrives.
static void keyboard_process(uint16_t hall_data[HALL_NUM_ADC][HALL_SLOTS_PER_ADC])
{
  uint16_t frame[1 + KEYBOARD_NUM_KEYS];

  frame[0] = read_wing_id();
  for (int a = 0; a < HALL_NUM_ADC; a++)
    for (int s = 0; s < HALL_SLOTS_PER_ADC; s++)
      frame[1 + a * HALL_SLOTS_PER_ADC + s] = hall_data[a][s];

  uint32_t sr_before = hspi1.Instance->SR;
  uint32_t cr1_before = hspi1.Instance->CR1;
  HAL_StatusTypeDef spi_status = HAL_SPI_Transmit(&hspi1, (uint8_t *)frame, 1 + KEYBOARD_NUM_KEYS, 500);
  if (spi_status != HAL_OK)
    printf("SPI transmit failed: size=%u status=%d error=0x%lx SR_before=0x%lx CR1_before=0x%lx SR=0x%lx CR1=0x%lx\r\n",
           (unsigned)(1 + KEYBOARD_NUM_KEYS), (int)spi_status, (unsigned long)hspi1.ErrorCode,
           (unsigned long)sr_before, (unsigned long)cr1_before,
           (unsigned long)hspi1.Instance->SR, (unsigned long)hspi1.Instance->CR1);

  // HAL_SPI_Transmit leaves SPE set after a successful transfer, but with CRC
  // enabled the next call's SPI_RESET_CRC (toggling CRCEN) is only valid while
  // SPE=0 (RM0440). Disable here so each frame starts from a clean state.
  __HAL_SPI_DISABLE(&hspi1);

  // CRCNEXT is left set (HAL never clears it once the data write races ahead
  // of the shift register for single-word transfers), so the next transfer
  // starts with the peripheral expecting to send a CRC instead of data. Clear
  // it explicitly so each frame starts from a known state.
  CLEAR_BIT(hspi1.Instance->CR1, SPI_CR1_CRCNEXT);

  static uint32_t last_tick = 0;
  uint32_t now = HAL_GetTick();
  if (now - last_tick >= 500)
  {
    last_tick = now;
    printf("SPI frame: wing=%u", (unsigned)frame[0]);
    for (int i = 0; i < KEYBOARD_NUM_KEYS; i++)
      printf(" %u", (unsigned)frame[1 + i]);
    printf("\r\n");
  }
}

// The wing id is strapped on fixed ID0..ID2 pins, so read it once on first
// use and cache it forever.
static uint8_t read_wing_id(void)
{
  static int cached = 0;
  static uint8_t id = 0;
  if (!cached)
  {
    if (HAL_GPIO_ReadPin(ID0_GPIO_Port, ID0_Pin) == GPIO_PIN_SET) id |= 1;
    if (HAL_GPIO_ReadPin(ID1_GPIO_Port, ID1_Pin) == GPIO_PIN_SET) id |= 2;
    if (HAL_GPIO_ReadPin(ID2_GPIO_Port, ID2_Pin) == GPIO_PIN_SET) id |= 4;
    cached = 1;
  }
  return id;
}

typedef struct {
  GPIO_TypeDef *port;
  uint16_t pin;
  int remaining;
  uint32_t last_tick;
  int led_on;
} blink_state_t;

static volatile blink_state_t g_blink;

// Toggled by the "show_keys" console command; main loop prints g_hall_data
// as a table at SHOW_KEYS_PERIOD_MS while set.
#define SHOW_KEYS_PERIOD_MS 50
static volatile int g_show_keys = 0;

// Toggled by the "show_stats" console command; adds min/max/stddev tables
// below the live readings while g_show_keys is also set.
static volatile int g_show_stats = 0;

// Emits one shared-report line as a console row: print it, clear to end of line
// (so a shorter row doesn't leave stale characters from the previous frame) and
// advance to the next line.
static void hall_report_emit(void *ctx, const char *line)
{
  (void)ctx;
  printf("%s" ANSI_CLEAR_LINE_END "\r\n", line);
}

static void print_hall_table(uint16_t hall_data[HALL_NUM_ADC][HALL_SLOTS_PER_ADC], uint32_t scan_freq_hz)
{
  // Save the cursor (sitting in the prompt line), hide it while redrawing the
  // table in place at the top of the screen (avoids it visibly jumping
  // through the table), then restore position and visibility so the prompt
  // and any partially-typed command are undisturbed.
  printf(ANSI_CURSOR_SAVE ANSI_CURSOR_HIDE ANSI_CURSOR_HOME);
  printf("scan freq   : %6lu Hz" ANSI_CLEAR_LINE_END "\r\n", (unsigned long)scan_freq_hz);

  hall_report_keys((const uint16_t *)hall_data, hall_report_emit, NULL);
  if (g_show_stats)
    hall_report_stats(&g_hall_stats, NULL, hall_report_emit, NULL);

  printf(ANSI_CURSOR_RESTORE ANSI_CURSOR_SHOW);
}

static void blink_tick(void)
{
  if (g_blink.remaining == 0)
    return;
  uint32_t now = HAL_GetTick();
  if (now - g_blink.last_tick < 200)
    return;
  g_blink.last_tick = now;
  if (g_blink.led_on)
  {
    HAL_GPIO_WritePin(g_blink.port, g_blink.pin, GPIO_PIN_RESET);
    g_blink.led_on = 0;
    g_blink.remaining--;
  }
  else
  {
    HAL_GPIO_WritePin(g_blink.port, g_blink.pin, GPIO_PIN_SET);
    g_blink.led_on = 1;
  }
}

extern "C" int console_execute(int argc, const char * const *argv)
{
  if (argc == 0)
    return 0;
  if (strcmp(argv[0], "hello") == 0)
    printf("Hello from Wing %u!\r\n", read_wing_id());
  else if (strcmp(argv[0], "blink") == 0 && argc >= 2)
  {
    GPIO_TypeDef *port = NULL;
    uint16_t pin = 0;
    if (strcmp(argv[1], "fn0") == 0)       { port = LED_FN0_GPIO_Port; pin = LED_FN0_Pin; }
    else if (strcmp(argv[1], "ready") == 0) { port = READY_GPIO_Port;   pin = READY_Pin;   }
    else printf("Unknown LED: %s (fn0, ready)\r\n", argv[1]);
    if (port)
    {
      g_blink.port = port;
      g_blink.pin = pin;
      g_blink.remaining = 5;
      g_blink.last_tick = HAL_GetTick();
      g_blink.led_on = 0;
    }
  }
  else if (strcmp(argv[0], "key") == 0)
  {
    g_show_keys = !g_show_keys;
    printf("show_keys: %s\r\n", g_show_keys ? "on" : "off");
  }
  else if (strcmp(argv[0], "stat") == 0)
  {
    g_show_stats = !g_show_stats;
    printf("show_stats: %s\r\n", g_show_stats ? "on" : "off");
  }
  else
    printf("Unknown command: %s\r\n", argv[0]);
  return 0;
}

void main_init(void)
{
  console_init(&huart1, USART1_IRQn);
  printf("SPI1 init: APB2ENR=0x%lx CR1=0x%lx CR2=0x%lx SR=0x%lx\r\n",
         (unsigned long)RCC->APB2ENR, (unsigned long)SPI1->CR1,
         (unsigned long)SPI1->CR2, (unsigned long)SPI1->SR);
}

void main_task(void)
{
  static uint8_t fn0_prev = 0xFF;
  static uint32_t poll_count = 0;
  static uint32_t last_poll_tick = 0;
  static uint32_t last_stats_reset_tick = 0;

  uint8_t fn0 = HAL_GPIO_ReadPin(SW_FN0_GPIO_Port, SW_FN0_Pin) == GPIO_PIN_RESET ? 1 : 0;
  if (fn0 != fn0_prev)
  {
    fn0_prev = fn0;
    printf("FN0: %c\r\n", fn0 ? '1' : '0');
  }

  blink_tick();

  hall_keyboard_scan(g_hall_data);
  hall_stats_update(&g_hall_stats, (const uint16_t *)g_hall_data);
  keyboard_process(g_hall_data);

  poll_count++;
  uint32_t now = HAL_GetTick();
  uint32_t elapsed = now - last_poll_tick;

  if (now - last_stats_reset_tick >= STATS_RESET_PERIOD_MS)
  {
    hall_stats_reset_window(&g_hall_stats);
    last_stats_reset_tick = now;
  }

  if (g_show_keys)
  {
    if (elapsed >= SHOW_KEYS_PERIOD_MS)
    {
      print_hall_table(g_hall_data, poll_count * 1000u / elapsed);
      poll_count = 0;
      last_poll_tick = now;
    }
  }
  else
  {
    if (elapsed >= 1000)
    {
      printf("poll freq   : %6lu Hz\r\n",
             (unsigned long)(poll_count * 1000u / elapsed));
      poll_count = 0;
      last_poll_tick = now;
    }
    report_hall_changes(g_hall_data);
  }

  console_poll();
  if (console_take_dirty()) console_redraw_prompt();
}
