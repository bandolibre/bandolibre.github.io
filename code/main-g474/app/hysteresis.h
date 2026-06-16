#ifndef HYSTERESIS_H_
#define HYSTERESIS_H_

/* Directional ("backlash" / play-operator) hysteresis for turning a noisy,
 * high-resolution sample into a clean low-resolution output (e.g. an 8-bit MIDI
 * CC), with a per-call decision on whether the output is worth emitting.
 *
 * Why not a symmetric delta: a plain "only change once the sample has moved by
 * +/-delta" dead-band suppresses a clean sample that has genuinely reached the
 * next output code, just because it sits inside the band -> latency for no noise
 * reason. Here the dead zone is asymmetric around an anchor and tied to motion
 * direction: moving WITH the current direction only needs to clear a small
 * forward play (near-zero added latency), while reversing must overcome a larger
 * reverse play; crossing it flips the direction so the thresholds swap. This is
 * the textbook backlash/play operator, the same jitter-vs-lag idea as the 1 euro
 * filter, expressed as hysteresis.
 *
 * Pipeline per sample: optional EMA low-pass -> directional backlash on the raw
 * value -> linear scale+clamp to 0..out_max -> rate-limit that coalesces (never
 * drops) a pending change.
 *
 * Thresholds are in raw input units; the EMA factor is fixed-point /256; the
 * period is in milliseconds. HAL-free: the caller passes the current time, so
 * this header compiles and is unit-tested on the host. hyst_update() is
 * static inline so a config built from g_properties at the call site scalarizes
 * away (zero-cost). */

#include <stdint.h>
#include <stdbool.h>

#define HYST_EMA_UNITY 256u   /* ema_alpha that disables filtering (pass-through) */

typedef struct {
  uint16_t in_min, in_max;   /* raw range mapped to 0..out_max (clamped) */
  uint8_t  out_max;          /* top output code, e.g. 127 */
  uint16_t fwd_thresh;       /* raw units: play in the current direction (small) */
  uint16_t rev_thresh;       /* raw units: play required to reverse (larger) */
  uint16_t ema_alpha;        /* fixed-point /256; HYST_EMA_UNITY = no filtering */
  uint16_t min_period_ms;    /* min ms between emits; 0 = no rate limit */
} hyst_config_t;

typedef struct {
  bool     init;             /* false until the first sample seeds the state */
  uint32_t filtered;         /* EMA accumulator, fixed-point (<<8) */
  uint32_t anchor;           /* backlash anchor in raw units */
  int8_t   dir;              /* +1 rising / -1 falling */
  uint8_t  last_out;         /* last value reported as "push" (valid if have_out) */
  bool     have_out;
  uint32_t last_emit_ms;
} hyst_state_t;

/* Linear interpolation of a raw value over [min,max] to 0..out_max, clamped at
 * both ends. Returns 0 for a degenerate (max <= min) range. */
static inline uint8_t hyst_scale(uint32_t value, uint16_t min, uint16_t max, uint8_t out_max)
{
  if (max <= min || value <= min) return 0;
  if (value >= max) return out_max;
  return (uint8_t)(((value - min) * (uint32_t)out_max) / (max - min));
}

/* Feed one raw sample. Updates *st, writes the scaled 0..out_max value to *out,
 * and returns true when the caller should push *out over MIDI. */
static inline bool hyst_update(hyst_state_t *st, const hyst_config_t *cfg,
                               uint32_t sample, uint32_t now_ms, uint8_t *out)
{
  /* Seed on first sample so the filter/anchor start at the real value rather
   * than drifting up from zero. */
  if (!st->init)
  {
    st->init = true;
    st->filtered = sample << 8;
    st->anchor = sample;
    st->dir = 1;
    st->have_out = false;
  }

  /* 1. Optional EMA low-pass. UNITY disables it (exact pass-through). */
  uint32_t x;
  if (cfg->ema_alpha >= HYST_EMA_UNITY)
  {
    x = sample;
    st->filtered = sample << 8;   /* keep accumulator tracking for a later enable */
  }
  else
  {
    /* filtered += alpha*(sample - filtered), all in <<8 fixed point. Done with a
     * signed delta so the filter can move down as well as up. */
    int32_t target = (int32_t)(sample << 8);
    st->filtered = (uint32_t)((int32_t)st->filtered +
                              ((target - (int32_t)st->filtered) * (int32_t)cfg->ema_alpha) / 256);
    x = st->filtered >> 8;
  }

  /* 2. Directional backlash on x. Moving with dir only needs to clear fwd_thresh;
   * reversing must clear the larger rev_thresh and flips dir. */
  if (st->dir >= 0)
  {
    if (x > st->anchor + cfg->fwd_thresh)        st->anchor = x - cfg->fwd_thresh;
    else if (x + cfg->rev_thresh < st->anchor) { st->anchor = x + cfg->rev_thresh; st->dir = -1; }
  }
  else
  {
    if (x + cfg->fwd_thresh < st->anchor)        st->anchor = x + cfg->fwd_thresh;
    else if (x > st->anchor + cfg->rev_thresh) { st->anchor = x - cfg->rev_thresh; st->dir = 1; }
  }

  /* 3. Quantize the anchor to the output range. */
  uint8_t value = hyst_scale(st->anchor, cfg->in_min, cfg->in_max, cfg->out_max);
  *out = value;

  /* 4. Rate-limit without dropping: a change stays pending across polls (the
   * anchor keeps tracking, so we always emit the latest value), and fires as
   * soon as the period has elapsed. The first emit is never delayed. */
  bool changed = !st->have_out || value != st->last_out;
  bool period_ok = !st->have_out || (now_ms - st->last_emit_ms) >= cfg->min_period_ms;
  if (!(changed && period_ok)) return false;

  st->last_out = value;
  st->last_emit_ms = now_ms;
  st->have_out = true;
  return true;
}

#endif /* HYSTERESIS_H_ */
