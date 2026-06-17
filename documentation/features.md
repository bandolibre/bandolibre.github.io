# Features

## MIDI Active Sensing

A heartbeat the device sends to the host so the host can detect a dropped
connection and cut the sound (silence stuck notes) when the heartbeat stops.

Settings:
- `midi_active_sensing_enable` — send the heartbeat (default on).
- `midi_active_sensing_period` — milliseconds between bytes (default 200).

## Table mode

The **left function button (FN0)** toggles table mode on and off; the current
state is reported on the console.

Normally a note only sounds while the bellows is moving, and its pitch and
velocity follow the bellows direction and intensity. Table mode lets the
instrument be played flat on a table with the bellows at rest: each key press
sounds immediately, the bellows is treated as always **pulled** (so every key
plays its pull note), and notes use a fixed velocity of 80. This makes it
practical to type a score into a DAW or notation software one note at a time,
without having to work the bellows.

Toggling table mode off re-evaluates the keys currently held so sounding notes
follow the real bellows again.

## Bellows sensitivity

The **middle function button (FN1)** cycles bellows sensitivity through three
levels, wrapping back to the first; the current level is reported on the
console. Each level scales the bellows signal (the 0..1024 intensity that drives
both note velocity and the expression CC), so a higher level reaches full
velocity and full expression with less bellows travel — useful for quiet playing
or a stiff bellows.

Level 1 is unity (no scaling). Levels 2 and 3 multiply by `bellow_scale_mid`
(default x1.5) and `bellow_scale_high` (default x2.0); both are stored as a /256
fixed point value (256 = x1.0), so 384 and 512. The scaled intensity is clamped
to its full range, so beyond the point that reaches maximum the signal simply
saturates.

The same multiplier scales the table-mode velocity, so switching sensitivity
levels also raises or lowers how hard table-mode notes play.

## Bellows inertia mode (FN2)

The **right function button (FN2)** toggles bellows inertia mode; the current
state is reported on the console. Off, the bellows reading drives the sound
directly. On, the reading is run through a virtual-bellows pressure model that
gives the light blade spring the feel of an acoustic bandoneon's bellows: a quick
impulse stores energy that stays available for the note played just after, held
chords soften as air escapes through the open pallets (pressed keys), and the
push/pull direction is committed so it does not flicker near rest.

The behaviour is shaped by the `bellow_inertia_*` properties (responsiveness,
damping, impulse strength, air leak, and the direction deadzone/hysteresis). See
[bellow_simulation.md](bellow_simulation.md) for the principle, the model, and a
tuning guide.

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
