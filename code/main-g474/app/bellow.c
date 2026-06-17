#include "bellow.h"

#include <math.h>
#include <stdio.h>

#include "buttons.h"
#include "console.h"
#include "hysteresis.h"
#include "keyboard.h"   /* L_MIDI_CH / R_MIDI_CH */
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

/* Inertia model (FN2): a virtual bellows with mass. The player's force F drives a
 * damped oscillator whose pressure P is the simulated intensity; P is pumped by
 * the bellows velocity v and bled by air escaping through the open pallets (keys).
 * All signed/algebraic (pull +, push -). Always integrated so the live report can
 * show it, but only fed to consumers when inertia mode is engaged. See
 * documentation/bellow_simulation.md. */
static float    g_bellow_v;            /* bellows velocity (momentum) */
static float    g_bellow_p;            /* chamber pressure (signed); the model output */
static uint16_t g_bellow_eff_intensity = 0;  /* |P| committed, 0..1024 */
static bellows_t g_bellow_eff_dir = BELLOWS_NEUTRAL;

/* Combined-hall calibration: center is the at-rest reading, hard push/pull
 * the readings at full travel. The deadzone sets how far from center the
 * bellows must move to leave BELLOWS_NEUTRAL (no air moves there, so
 * note_table maps every key to NOTE_NONE). The hysteresis margin then has to
 * be given back before returning to NEUTRAL, so a bellows resting right at
 * the deadzone edge doesn't chatter between NEUTRAL and PUSH/PULL. These are
 * the bellow_* properties in g_properties (properties.h). */

bellows_t bellow_direction(void)
{
  /* In inertia mode the committed direction comes from the simulated pressure, so
   * stored energy keeps the direction non-neutral briefly after the blade returns
   * (a note pressed just after an impulse still sounds in that direction). */
  return buttons_bellow_inertia() ? g_bellow_eff_dir : g_bellows;
}

uint16_t bellow_intensity(void)
{
  /* In inertia mode (FN2) consumers see the simulated pressure, so the energy
   * built up by a fast impulse stays available after the impulse ends. */
  return buttons_bellow_inertia() ? g_bellow_eff_intensity : g_bellow_intensity;
}

/* Bellows sensitivity multiplier (Q8, 256 = x1.0) for the level FN1 currently
 * selects: level 0 is unity, levels 1 and 2 use the bellow_scale_mid/high
 * properties. Applied to the intensity, so it scales both note velocity and
 * CC#11; also reused for the table-mode velocity. */
uint16_t bellow_sens_scale_q8(void)
{
  switch (buttons_bellow_sens_level())
  {
    case 1:  return g_properties->bellow_scale_mid;
    case 2:  return g_properties->bellow_scale_high;
    default: return 256;
  }
}

/* Classifies a (signed) reading into PUSH/NEUTRAL/PULL around a center, with a
 * deadzone of half-width dead (no air moves there) and a hysteresis margin that
 * must be given back before returning to NEUTRAL, so a reading resting at the
 * deadzone edge doesn't chatter. Convention: below center-dead is PUSH, above
 * center+dead is PULL. Shared by the raw hall path (center = bellow_center) and
 * the inertia model (center = 0, pressure units). */
static bellows_t bellow_classify(bellows_t prev, int32_t value,
                                 int32_t center, int32_t dead, int32_t hyst)
{
  int32_t push_edge = center - dead;
  int32_t pull_edge = center + dead;

  switch (prev)
  {
    case BELLOWS_PUSH:
      if (value >= push_edge + hyst) return BELLOWS_NEUTRAL;
      return BELLOWS_PUSH;
    case BELLOWS_PULL:
      if (value <= pull_edge - hyst) return BELLOWS_NEUTRAL;
      return BELLOWS_PULL;
    default:
      if (value < push_edge) return BELLOWS_PUSH;
      if (value > pull_edge) return BELLOWS_PULL;
      return BELLOWS_NEUTRAL;
  }
}

/* Updates direction/intensity from the combined hall reading (see bellow_poll). */
static void bellow_update(uint32_t hall_total)
{
  uint32_t center = g_properties->bellow_center;
  uint32_t push_edge = center - g_properties->bellow_dead;
  uint32_t pull_edge = center + g_properties->bellow_dead;

  g_bellows = bellow_classify(g_bellows, (int32_t)hall_total, (int32_t)center,
                              g_properties->bellow_dead, g_properties->bellow_hyst);

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

  /* Scale by the selected sensitivity level (Q8, >>8 to divide), clamping back
   * into 0..1024 so a higher scale reaches full intensity (and thus full
   * velocity/CC) sooner. */
  uint32_t scaled = ((uint32_t)g_bellow_intensity * bellow_sens_scale_q8()) >> 8;
  g_bellow_intensity = (uint16_t)(scaled > 1024 ? 1024 : scaled);
}

/* Integrates the inertia model (FN2) one step from the live reading. The player's
 * signed force F (magnitude g_bellow_intensity, sign from g_bellows) drives a
 * virtual bellows of mass: velocity v carries momentum, pressure P is pumped by v
 * and bled by air escaping through the open pallets (pressed keys). P is the simulated
 * intensity; at steady force it settles to F (reaches the input), while a fast
 * impulse leaves P elevated for the short window the player plays into. See
 * documentation/bellow_simulation.md for the derivation. */
static void bellow_simulate(void)
{
  static float    f_prev;
  static uint32_t last_tick;
  static bool     have_last;
  static bool     inertia_was_on;

  float F = (g_bellows == BELLOWS_PUSH) ? -(float)g_bellow_intensity
          : (g_bellows == BELLOWS_PULL) ?  (float)g_bellow_intensity
          : 0.0f;

  uint32_t now = HAL_GetTick();

  /* Seed on the first poll and re-seed when FN2 is freshly engaged, so the state
   * starts at the live force and the mode never jumps the sound or direction. */
  bool inertia = buttons_bellow_inertia();
  if (!have_last || (inertia && !inertia_was_on))
  {
    have_last = true;
    inertia_was_on = inertia;
    last_tick = now;
    f_prev = F;
    g_bellow_v = 0.0f;
    g_bellow_p = F;
    g_bellow_eff_intensity = (uint16_t)(fabsf(F) + 0.5f);
    g_bellow_eff_dir = g_bellows;
    return;
  }
  inertia_was_on = inertia;

  /* Clamp the step so a stalled loop can't blow up the explicit integrator. */
  float dt_s = (float)(now - last_tick) / 1000.0f;
  last_tick = now;
  if (dt_s > 0.02f) dt_s = 0.02f;

  float omega = 1000.0f / (float)g_properties->bellow_inertia_track_ms;
  float zeta  = (float)g_properties->bellow_inertia_damping / 256.0f;

  /* Velocity: an explicit impulse kick from the flexion speed (catches a sharp
   * impulse a sluggish oscillator would miss), then the damped restoring drive
   * toward F. Semi-implicit Euler: P below is pumped by this new v. */
  g_bellow_v += (float)g_properties->bellow_inertia_impulse_gain / 256.0f * (F - f_prev);
  g_bellow_v += (omega * omega * (F - g_bellow_p) - 2.0f * zeta * omega * g_bellow_v) * dt_s;
  f_prev = F;

  g_bellow_p += g_bellow_v * dt_s;

  /* Pressure leaks through the open pallets: faster the more keys are held (air
   * escapes whether or not the reed sounds, e.g. keys held at neutral before an
   * impulse). Clamped Euler (factor floored at 0) is stable, no transcendental. */
  float leak = ((float)g_properties->bellow_inertia_leak_quiet +
                (float)g_properties->bellow_inertia_leak_per_key *
                (float)keyboard_keys_pressed()) / 256.0f;
  float decay = 1.0f - leak * dt_s;
  if (decay < 0.0f) decay = 0.0f;
  g_bellow_p *= decay;

  if (g_bellow_p >  1024.0f) g_bellow_p =  1024.0f;
  if (g_bellow_p < -1024.0f) g_bellow_p = -1024.0f;

  /* Commit the effective intensity (magnitude, responsive) and direction (gated
   * only at the zero crossing by the shared deadzone+hysteresis, so push<->pull
   * can't chatter and retrigger the wrong bisonoric note). */
  g_bellow_eff_intensity = (uint16_t)(fabsf(g_bellow_p) + 0.5f);
  g_bellow_eff_dir = bellow_classify(g_bellow_eff_dir, (int32_t)g_bellow_p, 0,
                                     g_properties->bellow_inertia_dir_dead,
                                     g_properties->bellow_inertia_dir_hyst);
}

/* Emits CC#11 (Expression) from the effective intensity through the shared
 * directional-hysteresis + rate-limit pipeline (hysteresis.h, as the pedals use).
 * bellow_cchyst sets the play required before the CC moves (suppresses sensor
 * jitter) and bellow_cc_period_ms caps the send rate; the rate limit coalesces
 * rather than drops, so the latest value is always eventually sent. Fed by
 * bellow_intensity(), so the naive and inertia modes share one CC pipeline. */
static void bellow_send_cc(void)
{
  static hyst_state_t st;

  hyst_config_t cfg = {
    .in_min = 0, .in_max = 1024, .out_max = 127,
    .fwd_thresh = g_properties->bellow_cchyst, .rev_thresh = g_properties->bellow_cchyst,
    .ema_alpha = HYST_EMA_UNITY, .min_period_ms = g_properties->bellow_cc_period_ms,
  };

  uint8_t value;
  if (!hyst_update(&st, &cfg, bellow_intensity(), HAL_GetTick(), &value)) return;

  /* The single bellows drives both keyboards, which play on separate MIDI
   * channels (L_MIDI_CH / R_MIDI_CH), so send the expression CC on both. */
  usb_app_midi_control_change(L_MIDI_CH, 11, value);
  usb_app_midi_control_change(R_MIDI_CH, 11, value);
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
    float force = (g_bellows == BELLOWS_PUSH) ? -(float)g_bellow_intensity
                : (g_bellows == BELLOWS_PULL) ?  (float)g_bellow_intensity : 0.0f;
    console_dash_println("BELLOW  dir=%-7s int=%4u  force=%+5d v=%+8.1f P=%+7.1f  eff=%4u(%s) keys=%u",
                         dir_name[g_bellows], g_bellow_intensity,
                         (int)force, (double)g_bellow_v, (double)g_bellow_p,
                         g_bellow_eff_intensity, dir_name[g_bellow_eff_dir],
                         keyboard_keys_pressed());
    console_dash_println("        hall0=%4u hall1=%4u total=%5u  (min %u max %u)  sample_count=%lu  conv=%.1fus",
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
  bellow_simulate();
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
