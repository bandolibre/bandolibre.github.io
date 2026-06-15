#include "pedals.h"

#include <stdio.h>

#include "keyboard.h"   /* L_MIDI_CH / R_MIDI_CH */
#include "main.h"
#include "properties.h"
#include "usb_app.h"

/* ADC handles for the two pedal wipers, defined by the CubeMX-generated main.c:
 * ADC1 reads pedal 1 (expression), ADC2 pedal 2 (sustain). */
extern ADC_HandleTypeDef hadc1;
extern ADC_HandleTypeDef hadc2;

/* Each pedal's wiper is interpolated over its [min,max] property range to 0..127
 * and sent on this controller. Pedal 1 uses Modulation (CC#1) and pedal 2 the
 * Foot Controller (CC#4): both are pre-mapped in most instruments, so the
 * pedals do something out of the box, and either can still be MIDI-learned to a
 * different VST parameter in the DAW. */
#define PEDAL1_CC 1
#define PEDAL2_CC 4

/* Movement of the raw wiper reading (out of the 12-bit ADC range) that must be
 * exceeded before a new diagnostic line is logged, so wiper noise on a
 * connected pedal doesn't flood the console. */
static const uint32_t PEDAL_WIPER_HYST = 16;

/* Interpolates a raw wiper reading to a 0..127 MIDI CC value over [min,max],
 * clamped at both ends. Returns 0 for a degenerate (max <= min) range. */
static uint8_t pedal_cc_value(uint32_t raw, uint16_t min, uint16_t max)
{
  if (max <= min || raw <= min) return 0;
  if (raw >= max) return 127;
  return (uint8_t)(((raw - min) * 127u) / (max - min));
}

/* Per-pedal state retained across polls. */
typedef struct {
  uint8_t  connected_prev;  /* connected flag at the last logged line (0xFF = none yet) */
  uint32_t raw_prev;        /* raw wiper reading at the last logged line */
  uint8_t  cc_prev;         /* last 0..127 CC value sent, valid only when have_cc */
  bool     have_cc;         /* a CC has been sent since the pedal was last connected */
} pedal_state_t;

/* Polls one pedal: logs detect/movement changes, and while a pedal is connected
 * emits its Effect Controller CC whenever the interpolated 0..127 value moves.
 * The detect line is pulled high in the no-pedal state (a normally-closed jack
 * switch biases it to VCC), so a connected pedal reads GPIO_PIN_RESET. */
static void pedal_poll_one(const char *name, GPIO_TypeDef *det_port, uint16_t det_pin,
                           ADC_HandleTypeDef *adc, uint8_t controller,
                           uint16_t min, uint16_t max, pedal_state_t *st)
{
  uint8_t connected = HAL_GPIO_ReadPin(det_port, det_pin) == GPIO_PIN_RESET;
  uint32_t raw = HAL_ADC_GetValue(adc);

  uint32_t delta = raw > st->raw_prev ? raw - st->raw_prev : st->raw_prev - raw;
  if (connected != st->connected_prev || (connected && delta > PEDAL_WIPER_HYST))
  {
    st->connected_prev = connected;
    st->raw_prev = raw;
    printf("%s connected=%u val=%u\r\n", name, connected, (unsigned)raw);
  }

  /* Only stream the controller while a pedal is plugged in; the detect line
   * floats otherwise. Resending starts fresh on reconnect. */
  if (!connected)
  {
    st->have_cc = false;
    return;
  }
  uint8_t value = pedal_cc_value(raw, min, max);
  if (st->have_cc && value == st->cc_prev) return;
  st->cc_prev = value;
  st->have_cc = true;
  /* Mirror the bellows expression CC: send on both keyboard channels so the
   * mapping works regardless of which channel the DAW listens on. */
  usb_app_midi_control_change(L_MIDI_CH, controller, value);
  usb_app_midi_control_change(R_MIDI_CH, controller, value);
}

void pedals_poll(void)
{
  static pedal_state_t pedal1 = { 0xFF, 0xFFFF, 0, false };
  static pedal_state_t pedal2 = { 0xFF, 0xFFFF, 0, false };

  /* Start both conversions before reading either, so they run concurrently on
   * ADC1/ADC2 rather than back to back. pedal_poll_one() reads the results. */
  HAL_ADC_Start(&hadc1);
  HAL_ADC_Start(&hadc2);
  HAL_ADC_PollForConversion(&hadc1, 1);
  HAL_ADC_PollForConversion(&hadc2, 1);

  pedal_poll_one("PEDAL1", EXP_PEDAL_INT_GPIO_Port, EXP_PEDAL_INT_Pin, &hadc1,
                 PEDAL1_CC, g_properties->pedal1_min, g_properties->pedal1_max, &pedal1);
  pedal_poll_one("PEDAL2", SUS_PEDAL_INT_GPIO_Port, SUS_PEDAL_INT_Pin, &hadc2,
                 PEDAL2_CC, g_properties->pedal2_min, g_properties->pedal2_max, &pedal2);
}
