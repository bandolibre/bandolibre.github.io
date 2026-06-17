# Bellows inertia simulation (FN2)

A virtual-bellows model that gives the lightweight blade-spring instrument the
feel of an acoustic bandoneon's bellows. It is toggled by the **right function
button (FN2)**; with it off, the bellows reading drives the sound directly (the
plain mode). This document explains the principle, the model, and how to tune it.

## Why

The blade spring is read by a pair of Hall-effect sensors that measure its
flexion. That flexion tracks the **force** the player applies through the hands
and knees, and it reads faithfully while the force is steady or changing slowly.

An acoustic bandoneon's bellows, however, has mass and encloses a volume of air.
Two things follow that the bare blade does not reproduce:

- **Inertia.** A quick heel or hand *impulse* sets the heavy bellows moving and
  that motion persists for a moment. Players exploit this: they give an impulse
  *before* a note and play the note in the short window after, spending the
  stored energy as attack. The light blade instead just springs back and bounces.
- **Air escaping through the open pallets.** Pressure in the bellows is what
  drives loudness. It builds as the bellows is worked and bleeds away as air flows
  out through whichever pallets are open — and a pallet opens when its **key** is
  pressed, whether or not the reed is sounding yet. So a held chord softens unless
  the player keeps pushing, pressure is conserved while no key is held, and keys
  held down at rest already bleed the pressure of an impulse that follows.

The inertia mode models a bellows with these properties and uses its simulated
**pressure** as the intensity that sets note velocity and expression.

## Principle

The model is a small lumped-physics system, integrated once per bellows poll. It
is **algebraic**: pull is positive, push is negative, so push and pull energy stay
separate and a pull impulse never leaks into a push note.

| Physical quantity        | In the model                                            |
|--------------------------|---------------------------------------------------------|
| Player force on the bellows | `F` — signed live reading (magnitude × direction)    |
| Bellows mass / momentum  | `v` — bellows velocity (carries the inertia)            |
| Chamber pressure (loudness) | `P` — the model output, becomes the intensity        |
| Air escaping through open pallets | a leak on `P` that grows with the number of pressed keys |

The player's force accelerates the bellows mass; the moving bellows pumps the
chamber pressure; the pressure pushes back on the bellows and bleeds out through
the open pallets. Worked steadily, the pressure settles to the applied force, so the
output reaches the input. Hit with a brief impulse, the momentum keeps pumping the
pressure after the force is gone, leaving it elevated for the short window the
player plays into.

## Model

State `v` and `P` are kept across polls; `F` is the signed live reading and `n`
the number of keys held down (open pallets, regardless of bellows direction). With
`dt` the time since the last poll (clamped to guard against a stalled loop):

```
omega = 1000 / track_ms            angular speed at which pressure tracks force
zeta  = damping / 256              damping ratio (256 = critically damped)

v += impulse_gain/256 * (F - F_prev)                  impulse kick from flexion speed
v += (omega^2 * (F - P) - 2*zeta*omega * v) * dt      force drives the damped bellows
P += v * dt                                           the moving bellows pumps pressure
P *= max(0, 1 - (leak_quiet + leak_per_key*n)/256 * dt)    open pallets bleed pressure
P  = clamp(P, -1024, +1024)
```

- **Reaches the input.** With no key held the system is a damped oscillator
  driven toward `F`, so at a steady force `P` settles to `F`. With keys held the
  leak makes `P` settle a little below `F` — the player must push harder to hold a
  chord, exactly as on the acoustic instrument.
- **Stored energy.** A brief spike in `F` injects velocity `v`; after the force
  returns, `v` keeps pumping `P` for roughly `1 / omega`, the window that carries
  the impulse into the note.
- **Flexion speed.** Fast flexion drives the stored energy two ways: through the
  lag error `F - P` (large only when the force outruns the smoothed pressure) and
  through the explicit `impulse_gain * (F - F_prev)` term, which catches a sharp
  impulse a sluggish oscillator would otherwise miss. Setting `impulse_gain` to 0
  leaves the pure oscillator.
- **Damping.** At critical damping (`zeta = 1`) the bellows does not overshoot or
  ring, unlike the springy blade. Lower values allow a deliberate, expressive
  overshoot.

The integration is semi-implicit (the new velocity pumps the pressure in the same
step), which keeps the oscillator stable, and the leak is a plain factor floored
at zero, which is stable for any step and needs no transcendental.

The model is integrated every poll so its state is always visible on the live
report for tuning; its output is used for sound only while FN2 is engaged.
Engaging FN2 reseeds the state at the current force, so switching modes never
jumps the sound.

## Direction and output

On a bisonoric instrument the push/pull decision selects a *different note*, so it
must not flicker near rest — but the loudness must stay responsive. These are
handled separately:

- **Magnitude.** The intensity is `|P|`, used directly, with no added lag.
- **Direction.** The sign of `P` is committed through a deadzone plus a hysteresis
  margin (the same classifier the plain mode uses on the raw reading): the
  direction only changes by crossing through NEUTRAL, and a margin must be given
  back before it returns to NEUTRAL. So a wobble around zero cannot retrigger the
  opposite note, while a deliberate reversal still passes. Because the deadzone
  holds the direction briefly while stored pressure remains, a note pressed just
  after an impulse still sounds in the right direction.

Expression (CC#11) is emitted from the same effective intensity through the shared
hysteresis and rate-limit pipeline used by the pedals: it suppresses jitter, caps
the send rate, and coalesces rather than drops, so the latest value is always
sent. The plain and inertia modes share this one CC pipeline.

## Parameters

All are `bellow_inertia_*` properties, editable live from the console. Fixed-point
values are noted as `/256` (256 = 1.0).

| Property                  | Default | Meaning / feel                                                                 |
|---------------------------|---------|--------------------------------------------------------------------------------|
| `track_ms`                | 40      | Time for pressure to track force. Smaller is snappier with a shorter impulse window; larger is more sluggish with a longer window. |
| `damping`                 | 256     | Damping ratio `/256`. 256 is critical (no bounce); below that allows expressive overshoot. |
| `impulse_gain`            | 64      | Feed-forward `/256` from flexion speed. Raises how strongly a sharp impulse stores energy; 0 disables it. |
| `leak_quiet`              | 64      | Pressure bleed per second `/256` while no key is held. Higher lets stored energy fade faster at rest. |
| `leak_per_key`            | 128     | Extra bleed per second `/256` for each key held (open pallet). Higher makes held chords soften (and the impulse window shorten) more as more keys are pressed. |
| `dir_dead`                | 24      | Neutral deadzone half-width, in pressure units. How much pressure is needed before a direction is taken. |
| `dir_hyst`                | 12      | Margin given back before returning to neutral, in pressure units. Larger holds the direction (and a note) longer after the blade returns. |

The expression-CC behaviour reuses the existing `bellow_cchyst` and
`bellow_cc_period_ms`; the FN1 sensitivity scale still applies, since the model is
fed the already-scaled reading.

## Tuning

Enable the bellows live report (`show_bellow`) and watch the first line: it shows
the signed `force`, the velocity `v`, the pressure `P`, the committed effective
intensity and direction, and the `keys` count.

1. With FN2 off, confirm `P` follows `force` and settles onto it under a slow,
   steady push or pull.
2. Set `track_ms` for the impulse window you want, then `damping` for how much (if
   any) overshoot to allow.
3. Give a sharp impulse and play a note just after; raise `impulse_gain` until the
   stored attack feels right.
4. Hold a chord while pushing and raise `leak_per_key` until the pressure droop
   matches the desired effort.
5. If the direction flickers at rest, raise `dir_dead`; if notes cut off too soon
   after the blade returns, raise `dir_hyst`.
