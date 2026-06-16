#include "midi.h"
#include "main.h"        /* HAL_GetTick */
#include "properties.h"
#include "usb_app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MIDI_DEFAULT_VELOCITY 64

void midi_poll(void)
{
  static uint32_t last_tick = 0;
  if (!g_properties->midi_active_sensing_enable) return;
  uint32_t now = HAL_GetTick();
  if ((now - last_tick) < g_properties->midi_active_sensing_period) return;
  last_tick = now;
  usb_app_midi_active_sensing();
}

/* Parse argv[idx] as an integer in [lo,hi]. Prints an error and returns false
 * if missing, not a number, or out of range. */
static bool parse_arg(int argc, const char *const *argv, int idx,
                      long lo, long hi, const char *what, long *out)
{
  if (idx >= argc) { printf("missing %s\r\n", what); return false; }
  char *end;
  long v = strtol(argv[idx], &end, 0);
  if (argv[idx][0] == '\0' || *end != '\0') { printf("not a number: %s\r\n", argv[idx]); return false; }
  if (v < lo || v > hi) { printf("%s out of range [%ld,%ld]: %ld\r\n", what, lo, hi, v); return false; }
  *out = v;
  return true;
}

static void cmd_note_on(int argc, const char *const *argv)
{
  long channel, note, velocity = MIDI_DEFAULT_VELOCITY;
  if (!parse_arg(argc, argv, 1, 1, 16, "channel", &channel) ||
      !parse_arg(argc, argv, 2, 0, 127, "note", &note))
  {
    printf("usage: send_note_on <channel 1-16> <note 0-127> [velocity 0-127]\r\n");
    return;
  }
  if (argc > 3 && !parse_arg(argc, argv, 3, 0, 127, "velocity", &velocity)) return;
  usb_app_midi_note_on((uint8_t)(channel - 1), (uint8_t)note, (uint8_t)velocity);
  printf("note on  ch %ld note %ld vel %ld\r\n", channel, note, velocity);
}

static void cmd_note_off(int argc, const char *const *argv)
{
  long channel, note;
  if (!parse_arg(argc, argv, 1, 1, 16, "channel", &channel) ||
      !parse_arg(argc, argv, 2, 0, 127, "note", &note))
  {
    printf("usage: send_note_off <channel 1-16> <note 0-127>\r\n");
    return;
  }
  usb_app_midi_note_off((uint8_t)(channel - 1), (uint8_t)note);
  printf("note off ch %ld note %ld\r\n", channel, note);
}

static void cmd_cc(int argc, const char *const *argv)
{
  long channel, controller, value;
  if (!parse_arg(argc, argv, 1, 1, 16, "channel", &channel) ||
      !parse_arg(argc, argv, 2, 0, 127, "controller", &controller) ||
      !parse_arg(argc, argv, 3, 0, 127, "value", &value))
  {
    printf("usage: send_cc <channel 1-16> <controller 0-127> <value 0-127>\r\n");
    return;
  }
  usb_app_midi_control_change((uint8_t)(channel - 1), (uint8_t)controller, (uint8_t)value);
  printf("cc       ch %ld ctrl %ld val %ld\r\n", channel, controller, value);
}

bool midi_console_execute(int argc, const char *const *argv)
{
  if (argc == 0) return false;
  if (strcmp(argv[0], "send_note_on") == 0)       cmd_note_on(argc, argv);
  else if (strcmp(argv[0], "send_note_off") == 0) cmd_note_off(argc, argv);
  else if (strcmp(argv[0], "send_cc") == 0)       cmd_cc(argc, argv);
  else return false;
  return true;
}

void midi_console_help(void)
{
  printf("\r\nMIDI commands (cable 0, channel 1-16, data 0-127):\r\n");
  printf("  send_note_on  <channel> <note> [velocity]   velocity defaults to %d\r\n", MIDI_DEFAULT_VELOCITY);
  printf("  send_note_off <channel> <note>\r\n");
  printf("  send_cc       <channel> <controller> <value>\r\n");
}

size_t midi_console_complete(const char *prefix, const char **out, size_t cap)
{
  static const char *const names[] = { "send_note_on", "send_note_off", "send_cc" };
  size_t n = 0;
  size_t plen = prefix ? strlen(prefix) : 0;
  for (size_t i = 0; i < sizeof(names) / sizeof(names[0]) && n < cap; i++)
    if (strncmp(names[i], prefix ? prefix : "", plen) == 0) out[n++] = names[i];
  return n;
}
