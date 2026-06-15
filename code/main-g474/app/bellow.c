#include "bellow.h"

#include <math.h>
#include <stdio.h>

#include "console.h"
#include "main.h"
#include "properties.h"
#include "report.h"
#include "usb_app.h"

static const char *const dir_name[] = { "PULL", "PUSH", "NEUTRAL" };

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

uint16_t bellow_intensity(void)
{
  return g_bellow_intensity;
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
 * noise doesn't flood the link with near-identical CCs. bellow_cc_period_ms caps how
 * often CC#11 is sent, independent of the hysteresis check, so a fast-moving
 * bellows can't flood the link. Both are g_properties fields. */
static void bellow_send_cc(void)
{
  static uint16_t last_intensity;
  static uint8_t have_last;
  static uint32_t last_tick;

  uint32_t now = HAL_GetTick();
  if (have_last && (now - last_tick) < g_properties->bellow_cc_period_ms) return;

  uint16_t intensity = g_bellow_intensity;
  uint16_t delta = intensity > last_intensity ? intensity - last_intensity : last_intensity - intensity;
  if (!have_last || delta >= g_properties->bellow_cchyst)
  {
    last_intensity = intensity;
    last_tick = now;
    have_last = 1;
    usb_app_midi_control_change(11, (uint8_t)(intensity >> 3));
  }
}

static void delay_us(uint32_t us)
{
  uint32_t cycles = us * (SystemCoreClock / 1000000U);
  uint32_t start = DWT->CYCCNT;
  while ((DWT->CYCCNT - start) < cycles);
}

/* One acquisition of both hall sensors and their combined reading. */
typedef struct {
  uint16_t hall0;
  uint16_t hall1;
  uint16_t total;
  uint32_t conv_cycles;  /* CPU cycles spent in the two ADC conversions */
} bellow_sample_t;

/* Reads both hall sensors and returns their values and combined total. */
static bellow_sample_t bellow_sample(void)
{
  /* The two hall sensors share a gate-switched supply (HALL_NEN). Enable it,
   * let the gate and sensor settle, sample both, then disable to save power. */
  HAL_GPIO_WritePin(HALL_NEN_GPIO_Port, HALL_NEN_Pin, GPIO_PIN_RESET);
  /* Settling budget: gate RC (R5||R6 * Ciss_Q1 = 909R * 130pF, 5t~600ns) +
   * VDDH caps (RDS_on_Q1 * (CP1+CP2) = ~120mO * 200nF, 5t~120ns) +
   * SC4015SO power-on start <1us (datasheet) => worst case <3us; the default
   * bellow_settle_us of 5us is ~1.7x margin. */
  delay_us(g_properties->bellow_settle_us);

  uint32_t conv_start = DWT->CYCCNT;
  HAL_ADC_Start(&hadc3);
  HAL_ADC_Start(&hadc4);
  HAL_ADC_PollForConversion(&hadc3, 10);
  uint32_t hall0 = HAL_ADC_GetValue(&hadc3);
  HAL_ADC_PollForConversion(&hadc4, 10);
  uint32_t hall1 = HAL_ADC_GetValue(&hadc4);
  uint32_t conv_cycles = DWT->CYCCNT - conv_start;

  HAL_GPIO_WritePin(HALL_NEN_GPIO_Port, HALL_NEN_Pin, GPIO_PIN_SET);

  bellow_sample_t s;
  s.hall0 = (uint16_t)hall0;
  s.hall1 = (uint16_t)hall1;
  s.total = (uint16_t)(hall0 + hall1);
  s.conv_cycles = conv_cycles;
  return s;
}

/* Emits the optional console log and live-report dashboard for one sample.
 * bellows_prev is the direction before bellow_update() ran this poll. */
static void bellow_report(const bellow_sample_t *s, bellows_t bellows_prev)
{
  static uint32_t hall_total_prev = 0xFFFFFFFF;

  /* Per-frame stats, accumulated every poll and reset after each emitted report
   * frame (and on (re-)enable so the first frame never shows stale data), so each
   * line reflects only the samples since the previous one. Extremes are of the
   * combined reading; the std (E[x^2]-E[x]^2) is per sensor, accumulated in
   * integers to keep the sums exact, with the count shown alongside. */
  static uint16_t hall_min, hall_max;
  static uint32_t n, sum0, sum1;
  static uint64_t sq0, sq1;
  static bool show_was_on;
  if (g_properties->show_bellow && !show_was_on)
  {
    hall_min = 0xFFFF; hall_max = 0;
    n = 0; sum0 = sum1 = 0; sq0 = sq1 = 0;
  }
  show_was_on = g_properties->show_bellow;
  if (s->total < hall_min) hall_min = s->total;
  if (s->total > hall_max) hall_max = s->total;
  n++;
  sum0 += s->hall0; sq0 += (uint32_t)s->hall0 * s->hall0;
  sum1 += s->hall1; sq1 += (uint32_t)s->hall1 * s->hall1;

  /* Always log a direction change; otherwise only when log_bellow is set and the
   * combined reading has moved enough to be worth a line. */
  uint32_t hall_total_delta = s->total > hall_total_prev ? s->total - hall_total_prev : hall_total_prev - s->total;
  if(g_properties->log_bellow)
  {
    if (g_bellows != bellows_prev || hall_total_delta > g_properties->bellow_cchyst)
    {
      hall_total_prev = s->total;
      printf("HALL0=%u HALL1=%u TOTAL=%u DIR=%s\r\n",
            (unsigned)s->hall0, (unsigned)s->hall1, (unsigned)s->total, dir_name[g_bellows]);
    }
  }

  if (g_report_due && g_properties->show_bellow)
  {
    float mean0 = (float)sum0 / n;
    float mean1 = (float)sum1 / n;
    float std0 = sqrtf((float)sq0 / n - mean0 * mean0);
    float std1 = sqrtf((float)sq1 / n - mean1 * mean1);
    float conv_us = (float)s->conv_cycles / (SystemCoreClock / 1000000.0f);
    console_dash_println("BELLOW  dir=%-7s intensity=%4u  hall0=%4u hall1=%4u total=%5u  (min %u max %u)  sample_count=%lu  conv=%.1fus",
                         dir_name[g_bellows], g_bellow_intensity,
                         (unsigned)s->hall0, (unsigned)s->hall1, (unsigned)s->total,
                         hall_min, hall_max, (unsigned long)n, (double)conv_us);
    console_dash_println("        std0=%7.2f std1=%7.2f", (double)std0, (double)std1);

    hall_min = 0xFFFF; hall_max = 0;
    n = 0; sum0 = sum1 = 0; sq0 = sq1 = 0;
  }
}

/* Samples both hall sensors, updates the bellows direction/intensity, emits the
 * expression CC, and reports. Call once per main loop iteration. Consumers read
 * the result via bellow_direction() and track changes themselves. */
void bellow_poll(void)
{
  bellow_sample_t s = bellow_sample();
  bellows_t bellows_prev = g_bellows;
  bellow_update(s.total);
  bellow_send_cc();
  bellow_report(&s, bellows_prev);
}

/* Diagnostic sweep: for each bellow_settle_us value in a fixed range, take
 * BELLOW_TUNE_SAMPLES samples (each spaced BELLOW_TUNE_PAUSE_MS apart) and print
 * the per-sensor mean and standard deviation. Helps pick the smallest settling
 * delay that still yields stable hall readings. Blocks the main loop for the
 * duration; temporarily overrides bellow_settle_us and restores it on return. */
void bellow_tune(void)
{
  enum {
    BELLOW_TUNE_LO        = 0,
    BELLOW_TUNE_HI        = 20,
    BELLOW_TUNE_STEP      = 2,
    BELLOW_TUNE_SAMPLES   = 100,
    BELLOW_TUNE_PAUSE_MS  = 20,
  };

  size_t idx;
  if (!property_by_name("bellow_settle_us", &idx))
  {
    printf("bellow_settle_us property not found\r\n");
    return;
  }
  uint16_t saved;
  property_get_u16(idx, &saved);

  /* stdout is block-buffered and the main loop (which would flush it) is stalled
   * for the whole sweep, so flush each row explicitly or nothing appears until
   * the command returns. */
  printf("settle_us   h1_avg  h1_std   h2_avg  h2_std\r\n");
  fflush(stdout);
  for (uint16_t us = BELLOW_TUNE_LO; us <= BELLOW_TUNE_HI; us += BELLOW_TUNE_STEP)
  {
    property_set_u16(idx, us);

    uint32_t sum0 = 0, sum1 = 0;
    uint64_t sq0 = 0, sq1 = 0;
    for (int i = 0; i < BELLOW_TUNE_SAMPLES; i++)
    {
      bellow_sample_t s = bellow_sample();
      sum0 += s.hall0; sq0 += (uint32_t)s.hall0 * s.hall0;
      sum1 += s.hall1; sq1 += (uint32_t)s.hall1 * s.hall1;
      /* This runs in the USART RX ISR (microrl execute callback), where SysTick
       * cannot preempt, so HAL_Delay would hang on a frozen tick. Busy-wait on
       * the DWT cycle counter instead. */
      delay_us(BELLOW_TUNE_PAUSE_MS * 1000U);
    }

    /* Mean and variance (E[x^2] - E[x]^2) in float; the FPU does the divides and
     * sqrt. Samples are accumulated in integers to keep the sums exact. */
    float mean0 = (float)sum0 / BELLOW_TUNE_SAMPLES;
    float mean1 = (float)sum1 / BELLOW_TUNE_SAMPLES;
    float std0 = sqrtf((float)sq0 / BELLOW_TUNE_SAMPLES - mean0 * mean0);
    float std1 = sqrtf((float)sq1 / BELLOW_TUNE_SAMPLES - mean1 * mean1);

    printf("%9u   %6.1f  %6.2f   %6.1f  %6.2f\r\n",
           us, (double)mean0, (double)std0, (double)mean1, (double)std1);
    fflush(stdout);
  }

  property_set_u16(idx, saved);
}
