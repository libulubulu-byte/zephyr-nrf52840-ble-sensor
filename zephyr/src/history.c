/*
 * Fixed-size ring buffer for offline samples.
 *
 * Implemented without locking: every access happens from the system work
 * queue, which serialises the sampler, the replay path and the GATT read
 * handler. If a second context ever needs access, add a k_mutex here.
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "history.h"

LOG_MODULE_REGISTER(history, LOG_LEVEL_INF);

static struct history_record ring[HISTORY_CAPACITY];
static size_t head;              /* next slot to write */
static size_t count;             /* valid records */
static uint32_t dropped;         /* records lost to overflow */

void history_init(void)
{
	memset(ring, 0, sizeof(ring));
	head = 0U;
	count = 0U;
	dropped = 0U;
}

bool history_push(const struct history_record *rec)
{
	bool had_room = (count < HISTORY_CAPACITY);

	ring[head] = *rec;
	head = (head + 1U) % HISTORY_CAPACITY;

	if (had_room) {
		count++;
		LOG_DBG("stored sample #%zu (temp=%d centi-C, hum=%u centi-%%RH)",
			count - 1U, rec->temp_c100, rec->hum_pct100);
	} else {
		dropped++;
		if ((dropped % 10U) == 1U) {
			LOG_WRN("history full, dropped %u record(s)", dropped);
		}
	}

	return had_room;
}

size_t history_count(void)
{
	return count;
}

uint32_t history_dropped(void)
{
	return dropped;
}

const struct history_record *history_at(size_t index)
{
	size_t oldest;

	if (index >= count) {
		return NULL;
	}

	/* `head` points one past the newest record, so the oldest record is
	 * HISTORY_CAPACITY slots back from it.
	 */
	oldest = (head + HISTORY_CAPACITY - count) % HISTORY_CAPACITY;

	return &ring[(oldest + index) % HISTORY_CAPACITY];
}

void history_clear(void)
{
	head = 0U;
	count = 0U;
}
