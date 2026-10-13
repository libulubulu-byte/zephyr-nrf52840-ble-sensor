/*
 * Ring buffer of environment samples taken while no central was connected.
 *
 * Records are kept in RAM only (System ON Idle keeps RAM alive, see the
 * power management notes in README.md). Once the buffer is full the oldest
 * record is overwritten and a drop counter is incremented, so the host can
 * tell that data was lost rather than silently receiving a shorter history.
 */

#ifndef ENV_HISTORY_H_
#define ENV_HISTORY_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Maximum number of stored samples. */
#define HISTORY_CAPACITY 100

/** One stored sample. Packed so it can be pushed straight onto the air. */
struct history_record {
	uint32_t uptime_s;   /**< seconds since boot when the sample was taken */
	int16_t temp_c100;   /**< temperature in 0.01 degC */
	uint16_t hum_pct100; /**< relative humidity in 0.01 %RH */
} __packed;

/** Reset the ring buffer to empty. */
void history_init(void);

/**
 * Append a sample.
 *
 * @return true if the buffer was not full, false if the oldest record had to
 *         be discarded to make room (see history_dropped()).
 */
bool history_push(const struct history_record *rec);

/** Number of valid records currently stored (0..HISTORY_CAPACITY). */
size_t history_count(void);

/** Number of records discarded because the buffer was full. */
uint32_t history_dropped(void);

/**
 * Access a record by age.
 *
 * @param index 0 = oldest record, history_count() - 1 = newest.
 * @return pointer into the buffer, or NULL if index is out of range.
 */
const struct history_record *history_at(size_t index);

/** Drop every stored record (called once the history has been replayed). */
void history_clear(void);

#endif /* ENV_HISTORY_H_ */
