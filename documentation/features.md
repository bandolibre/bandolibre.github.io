# Features

## MIDI Active Sensing

A heartbeat the device sends to the host so the host can detect a dropped
connection and cut the sound (silence stuck notes) when the heartbeat stops.

Settings:
- `midi_active_sensing_enable` — send the heartbeat (default on).
- `midi_active_sensing_period` — milliseconds between bytes (default 200).
