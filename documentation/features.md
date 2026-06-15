# Features

## MIDI Active Sensing

A heartbeat the device sends to the host so the host can detect a dropped
connection and cut the sound (silence stuck notes) when the heartbeat stops.

Settings:
- `midi_active_sensing_enable` — send the heartbeat (default on).
- `midi_active_sensing_period` — milliseconds between bytes (default 200).

## Pedals

Two pedal inputs, each accepting an expression pedal wired like the M-Audio
EX-P (mode switch set to **M-Audio**). A plugged-in pedal is read continuously
and sent on both keyboard channels: pedal 1 on the **Modulation wheel (CC#1)**,
pedal 2 on the **Foot Controller (CC#4)**. Both are pre-mapped in most
instruments, so the pedals are expressive out of the box, and either can be
MIDI-learned to another VST parameter in the DAW.

Per pedal, `pedalN_min`/`pedalN_max` set the raw ADC readings that map to CC 0
and CC 127; values between are interpolated linearly and values outside are
clamped. Setting this window inside the pedal's full travel lets the usable
stroke span the whole MIDI range. To calibrate, push the pedal to each extreme
and use the raw ADC readings there as min and max.
