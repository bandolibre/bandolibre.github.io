#include "bellow.h"

#include <stdio.h>

#include "main.h"
#include "properties.h"
#include "usb_app.h"

/* ADC handles for the two hall sensors, defined by the CubeMX-generated main.c. */
extern ADC_HandleTypeDef hadc3;
extern ADC_HandleTypeDef hadc4;

/* Direction holds and intensity (0..1024) of how hard the bellows is being
 * pushed or pulled; both are derived from the combined hall reading by
 * bellow_update(). */
static bellows_t g_bellows = BELLOWS_NEUTRAL;
static uint16_t g_bellow_intensity = 0;

/* Combined-hall calibration: center is the at-rest reading, hard push/pull
 * the readings at full travel. The deadzone sets how far from center the
 * bellows must move to leave BELLOWS_NEUTRAL (no air moves there, so
 * note_table maps every key to NOTE_NONE). The hysteresis margin then has to
 * be given back before returning to NEUTRAL, so a bellows resting right at
 * the deadzone edge doesn't chatter between NEUTRAL and PUSH/PULL. These are
 * the bellow_* properties in g_properties (properties.h). */

bellows_t bellow_direction(void)
{
  return g_bellows;
}

/* Updates direction/intensity from the combined hall reading (see bellow_poll). */
static void bellow_update(uint32_t hall_total)
{
  uint32_t center = g_properties->bellow_center;
  uint32_t hyst   = g_properties->bellow_hyst;
  uint32_t push_edge = center - g_properties->bellow_dead;
  uint32_t pull_edge = center + g_properties->bellow_dead;

  switch (g_bellows)
  {
    case BELLOWS_PUSH:
      if (hall_total >= push_edge + hyst) g_bellows = BELLOWS_NEUTRAL;
      break;
    case BELLOWS_PULL:
      if (hall_total <= pull_edge - hyst) g_bellows = BELLOWS_NEUTRAL;
      break;
    default:
      if (hall_total < push_edge) g_bellows = BELLOWS_PUSH;
      else if (hall_total > pull_edge) g_bellows = BELLOWS_PULL;
      break;
  }

  if (g_bellows == BELLOWS_PUSH)
  {
    uint32_t span = push_edge - g_properties->bellow_full_push;
    uint32_t d = hall_total < push_edge ? push_edge - hall_total : 0;
    g_bellow_intensity = (uint16_t)(d >= span ? 1024 : (d * 1024) / span);
  }
  else if (g_bellows == BELLOWS_PULL)
  {
    uint32_t span = g_properties->bellow_full_pull - pull_edge;
    uint32_t d = hall_total > pull_edge ? hall_total - pull_edge : 0;
    g_bellow_intensity = (uint16_t)(d >= span ? 1024 : (d * 1024) / span);
  }
  else
  {
    g_bellow_intensity = 0;
  }
}

/* CC#11 (Expression) hysteresis, in the same 0..1024 units as the intensity:
 * only resent once intensity has moved by at least bellow_cchyst, so sensor
 * noise doesn't flood the link with near-identical CCs. bellow_ccper caps how
 * often CC#11 is sent, independent of the hysteresis check, so a fast-moving
 * bellows can't flood the link. Both are g_properties fields. */
static void bellow_send_cc(void)
{
  static uint16_t last_intensity;
  static uint8_t have_last;
  static uint32_t last_tick;

  uint32_t now = HAL_GetTick();
  if (have_last && (now - last_tick) < g_properties->bellow_ccper) return;

  uint16_t intensity = g_bellow_intensity;
  uint16_t delta = intensity > last_intensity ? intensity - last_intensity : last_intensity - intensity;
  if (!have_last || delta >= g_properties->bellow_cchyst)
  {
    last_intensity = intensity;
    last_tick = now;
    have_last = 1;
    usb_app_midi_control_change(11, (uint8_t)((intensity * 127) / 1024));
  }
}

static void delay_us(uint32_t us)
{
  uint32_t cycles = us * (SystemCoreClock / 1000000U);
  uint32_t start = DWT->CYCCNT;
  while ((DWT->CYCCNT - start) < cycles);
}

/* Reads both hall sensors, updates the bellows direction/intensity, and emits
 * the expression CC. Call once per main loop iteration. Consumers read the
 * result via bellow_direction() and track changes themselves. */
void bellow_poll(void)
{
  static uint32_t hall_total_prev = 0xFFFFFFFF;

  /* The two hall sensors share a gate-switched supply (HALL_NEN). Enable it,
   * let the gate and sensor settle, sample both, then disable to save power. */
  HAL_GPIO_WritePin(HALL_NEN_GPIO_Port, HALL_NEN_Pin, GPIO_PIN_RESET);
  /* Settling budget: gate RC (R5||R6 * Ciss_Q1 = 909R * 130pF, 5t~600ns) +
   * VDDH caps (RDS_on_Q1 * (CP1+CP2) = ~120mO * 200nF, 5t~120ns) +
   * SC4015SO power-on start <1us (datasheet) => worst case <3us; 5us = ~1.7x margin. */
  delay_us(5);

  HAL_ADC_Start(&hadc3);
  HAL_ADC_Start(&hadc4);
  HAL_ADC_PollForConversion(&hadc3, 10);
  uint32_t hall0 = HAL_ADC_GetValue(&hadc3);
  HAL_ADC_PollForConversion(&hadc4, 10);
  uint32_t hall1 = HAL_ADC_GetValue(&hadc4);

  HAL_GPIO_WritePin(HALL_NEN_GPIO_Port, HALL_NEN_Pin, GPIO_PIN_SET);

  uint32_t hall_total = hall0 + hall1;
  bellow_update(hall_total);
  bellow_send_cc();

  uint32_t hall_total_delta = hall_total > hall_total_prev ? hall_total - hall_total_prev : hall_total_prev - hall_total;
  if (hall_total_delta > 16)
  {
    hall_total_prev = hall_total;
    printf("HALL0=%u HALL1=%u TOTAL=%u\r\n", (unsigned)hall0, (unsigned)hall1, (unsigned)hall_total);
  }
}
