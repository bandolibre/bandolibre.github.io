#include "bellow.h"
#include "main.h"
#include "properties.h"
#include "usb_app.h"

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

void bellow_update(uint32_t hall_total)
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
void bellow_send_cc(void)
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
