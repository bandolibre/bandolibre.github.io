#ifndef HALL_REPORT_H_
#define HALL_REPORT_H_

#include <stdint.h>

/* Hall-key console report, shared by the wing (which renders the keyboard it
 * just scanned) and the main board (which renders each wing's keyboard as
 * carried by the SPI frame). Both operate on the same flat key array: words
 * 1.. of the SPI frame (spi_link.h), i.e. one raw hall reading per physical
 * key in flattened key order.
 *
 * Key order. By construction of the wing scan, a key's array index is
 *   index = adc * HALL_REPORT_SLOTS_PER_ADC + sel * HALL_REPORT_NUM_RANK + rank.
 * The report never knows a key's true (adc, sel, rank) on the main side; it
 * simply reconstructs them from the array index so the table keeps the wing's
 * tabular shape (one row per ADC, columns sel.rank). Keys are laid out 0..n
 * left-to-right within each row, so the column order is just the frame order. */

#define HALL_REPORT_NUM_ADC        5
#define HALL_REPORT_NUM_SEL        4
#define HALL_REPORT_NUM_RANK       2
#define HALL_REPORT_SLOTS_PER_ADC  (HALL_REPORT_NUM_SEL * HALL_REPORT_NUM_RANK)
#define HALL_REPORT_NUM_KEYS       (HALL_REPORT_NUM_ADC * HALL_REPORT_SLOTS_PER_ADC)

/* Per-key running statistics, folded one key array at a time. min/max span
 * since the last hall_stats_init(); sum/sumsq/count cover the current window
 * (hall_stats_reset_window) so the derived stddev tracks only recent noise. */
typedef struct {
  uint16_t min[HALL_REPORT_NUM_KEYS];
  uint16_t max[HALL_REPORT_NUM_KEYS];
  uint64_t sum[HALL_REPORT_NUM_KEYS];
  uint64_t sumsq[HALL_REPORT_NUM_KEYS];
  uint32_t count;        /* samples folded into the current stddev window */
  uint8_t  minmax_init;  /* min/max seeded yet? (not cleared by reset_window) */
} hall_stats_t;

/* Clears everything, including min/max. */
void hall_stats_init(hall_stats_t *s);

/* Folds one HALL_REPORT_NUM_KEYS-long key array into the stats. */
void hall_stats_update(hall_stats_t *s, const uint16_t *keys);

/* Resets only the stddev window (sum/sumsq/count); min/max are kept. */
void hall_stats_reset_window(hall_stats_t *s);

/* Receives one fully-formatted line: no trailing newline, no CR-LF, no
 * line-clear. The caller positions and terminates it (raw printf on the wing,
 * the console dashboard on the main). May carry ANSI color escapes. */
typedef void (*hall_report_line_fn)(void *ctx, const char *line);

/* Emits the live key table: a column header followed by one row per ADC, each
 * cell color-coded by its reading, columns left-to-right in frame order. */
void hall_report_keys(const uint16_t *keys, hall_report_line_fn emit, void *ctx);

/* Emits the min, max, diff and stddev statistic tables for the given stats,
 * each color-coded relative to its own value range, separated by blank lines.
 * "prefix" (may be NULL) is prepended to every table title, e.g. the keyboard
 * name so a multi-keyboard report stays unambiguous ("SPI1/L min ..."). */
void hall_report_stats(const hall_stats_t *s, const char *prefix,
                       hall_report_line_fn emit, void *ctx);

#endif /* HALL_REPORT_H_ */
