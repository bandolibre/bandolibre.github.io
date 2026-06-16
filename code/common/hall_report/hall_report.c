#include "hall_report.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "ansi.h"

/* ===== Statistics ========================================================= */

void hall_stats_init(hall_stats_t *s)
{
  memset(s, 0, sizeof(*s));
}

void hall_stats_update(hall_stats_t *s, const uint16_t *keys)
{
  int first = !s->minmax_init;
  s->minmax_init = 1;
  s->count++;
  for (int k = 0; k < HALL_REPORT_NUM_KEYS; k++)
  {
    uint16_t v = keys[k];
    if (first || v < s->min[k]) s->min[k] = v;
    if (first || v > s->max[k]) s->max[k] = v;
    s->sum[k]   += v;
    s->sumsq[k] += (uint64_t)v * v;
  }
}

void hall_stats_reset_window(hall_stats_t *s)
{
  memset(s->sum, 0, sizeof(s->sum));
  memset(s->sumsq, 0, sizeof(s->sumsq));
  s->count = 0;
}

/* Integer square root (floor), via Newton's method. */
static uint32_t isqrt32(uint32_t x)
{
  if (x == 0) return 0;
  uint32_t r = x, r_prev;
  do {
    r_prev = r;
    r = (r + x / r) / 2;
  } while (r < r_prev);
  return r_prev;
}

/* ===== Line builder ======================================================= */

/* A row can carry HALL_REPORT_SLOTS_PER_ADC colored cells (each ~30 bytes for a
 * truecolor background escape + value + reset) plus a label, so size generously. */
typedef struct { char buf[384]; int len; } line_t;

static void line_reset(line_t *l) { l->len = 0; l->buf[0] = '\0'; }

static void line_addf(line_t *l, const char *fmt, ...)
{
  int room = (int)sizeof(l->buf) - l->len;
  if (room <= 1) return;
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(l->buf + l->len, room, fmt, ap);
  va_end(ap);
  if (n < 0) return;
  l->len += (n < room) ? n : (room - 1);
}

/* ===== Cell coloring ====================================================== */

/* Live-reading background: none at/above HALL_COLOR_HIGH, fading from blue (at
 * HALL_COLOR_HIGH) to orange (at/below HALL_COLOR_LOW), and dark grey text with
 * no background below HALL_COLOR_MIN. */
#define HALL_COLOR_HIGH 2040
#define HALL_COLOR_LOW  1500
#define HALL_COLOR_MIN  100
static void cell_reading(line_t *l, uint16_t val)
{
  if ((int)val < HALL_COLOR_MIN)
  {
    line_addf(l, ANSI_FG_GREY " %6u" ANSI_RESET, (unsigned)val);
    return;
  }
  if ((int)val >= HALL_COLOR_HIGH)
  {
    line_addf(l, " %6u", (unsigned)val);
    return;
  }
  int span = HALL_COLOR_HIGH - HALL_COLOR_LOW;
  int t = HALL_COLOR_HIGH - (int)val;
  if (t > span) t = span;
  int r = 255 * t / span;
  int g = 165 * t / span;
  int b = 255 - 255 * t / span;
  line_addf(l, ANSI_BG_RGB_FMT " %6u" ANSI_RESET, r, g, b, (unsigned)val);
}

/* Peak background intensity (0-255) for the statistic-table gradient; kept low
 * so the cells stay dark. */
#define HALL_STAT_COLOR_MAX 100

/* Statistic-table background, relative to the table's own [lo, hi] range (which
 * excludes zero cells): green near lo, red near hi, black in the middle third.
 * Zero cells are always dark grey with no background, regardless of range. */
static void cell_stat(line_t *l, uint16_t val, uint16_t lo, uint16_t hi)
{
  if (val == 0)
  {
    line_addf(l, ANSI_FG_GREY " %6u" ANSI_RESET, (unsigned)val);
    return;
  }
  int range = (int)hi - (int)lo;
  if (range <= 0)
  {
    line_addf(l, ANSI_BG_RGB_FMT " %6u" ANSI_RESET, 0, 0, 0, (unsigned)val);
    return;
  }
  int pos = (int)val - (int)lo;
  int third = range / 3;
  if (pos > third && pos < range - third)
  {
    line_addf(l, ANSI_BG_RGB_FMT " %6u" ANSI_RESET, 0, 0, 0, (unsigned)val);
    return;
  }
  if (pos <= third)
  {
    int denom = third > 0 ? third : range;
    int t = denom - pos;
    if (t > denom) t = denom;
    int g = HALL_STAT_COLOR_MAX * t / denom;
    line_addf(l, ANSI_BG_RGB_FMT " %6u" ANSI_RESET, 0, g, 0, (unsigned)val);
  }
  else
  {
    int denom = third > 0 ? third : range;
    int t = pos - (range - denom);
    if (t > denom) t = denom;
    int r = HALL_STAT_COLOR_MAX * t / denom;
    line_addf(l, ANSI_BG_RGB_FMT " %6u" ANSI_RESET, r, 0, 0, (unsigned)val);
  }
}

/* ===== Tables ============================================================= */

/* Column header: a blank for the row label, then "selS.R" per column, the
 * (sel, rank) reconstructed from the column's frame index. */
static void emit_header(hall_report_line_fn emit, void *ctx)
{
  line_t l;
  line_reset(&l);
  line_addf(&l, "     ");
  for (int s = 0; s < HALL_REPORT_SLOTS_PER_ADC; s++)
    line_addf(&l, " sel%d.%d", s / HALL_REPORT_NUM_RANK, s % HALL_REPORT_NUM_RANK);
  emit(ctx, l.buf);
}

void hall_report_keys(const uint16_t *keys, hall_report_line_fn emit, void *ctx)
{
  emit_header(emit, ctx);
  for (int a = 0; a < HALL_REPORT_NUM_ADC; a++)
  {
    line_t l;
    line_reset(&l);
    line_addf(&l, "adc%d ", a);
    for (int s = 0; s < HALL_REPORT_SLOTS_PER_ADC; s++)
      cell_reading(&l, keys[a * HALL_REPORT_SLOTS_PER_ADC + s]);
    emit(ctx, l.buf);
  }
}

/* One labelled statistic table, color-coded relative to its own min/max range
 * (zero cells excluded from the range and shown dark grey). "suffix" (may be
 * NULL) is appended to the title, e.g. to report the stddev sample count.
 * Followed by a trailing blank separator line. */
static void emit_value_table(const char *prefix, const char *label, const uint16_t *vals,
                             const char *suffix, hall_report_line_fn emit, void *ctx)
{
  uint16_t lo = 0, hi = 0;
  int have_range = 0;
  for (int k = 0; k < HALL_REPORT_NUM_KEYS; k++)
  {
    if (vals[k] == 0) continue;
    if (!have_range || vals[k] < lo) lo = vals[k];
    if (!have_range || vals[k] > hi) hi = vals[k];
    have_range = 1;
  }

  line_t l;
  line_reset(&l);
  line_addf(&l, "----- %s%s%s (from %u to %u)%s -----",
            prefix ? prefix : "", prefix ? " " : "", label,
            (unsigned)lo, (unsigned)hi, suffix ? suffix : "");
  emit(ctx, l.buf);

  emit_header(emit, ctx);
  for (int a = 0; a < HALL_REPORT_NUM_ADC; a++)
  {
    line_reset(&l);
    line_addf(&l, "adc%d ", a);
    for (int s = 0; s < HALL_REPORT_SLOTS_PER_ADC; s++)
      cell_stat(&l, vals[a * HALL_REPORT_SLOTS_PER_ADC + s], lo, hi);
    emit(ctx, l.buf);
  }
  emit(ctx, "");
}

void hall_report_stats(const hall_stats_t *s, const char *prefix,
                       hall_report_line_fn emit, void *ctx)
{
  emit(ctx, "");
  emit_value_table(prefix, "min", s->min, NULL, emit, ctx);
  emit_value_table(prefix, "max", s->max, NULL, emit, ctx);

  uint16_t diff[HALL_REPORT_NUM_KEYS];
  for (int k = 0; k < HALL_REPORT_NUM_KEYS; k++)
    diff[k] = s->max[k] - s->min[k];
  emit_value_table(prefix, "diff", diff, NULL, emit, ctx);

  uint16_t stddev[HALL_REPORT_NUM_KEYS];
  uint32_t count = s->count ? s->count : 1;
  for (int k = 0; k < HALL_REPORT_NUM_KEYS; k++)
  {
    uint32_t mean    = (uint32_t)(s->sum[k]   / count);
    uint32_t mean_sq = (uint32_t)(s->sumsq[k] / count);
    uint32_t var = (mean_sq > mean * mean) ? mean_sq - mean * mean : 0;
    stddev[k] = (uint16_t)isqrt32(var);
  }
  char suffix[24];
  snprintf(suffix, sizeof(suffix), ", %lu samples", (unsigned long)s->count);
  emit_value_table(prefix, "stddev", stddev, suffix, emit, ctx);
}
