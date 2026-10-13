/*
 * Custom GATT service - see env_service.h for the UUID map.
 *
 * Design notes
 * ------------
 *  - Readable characteristics use explicit read callbacks with a user_data
 *    pointer, so the wire format is explicit and does not depend on struct
 *    packing. The nRF52840 is little endian, which is what the GATT
 *    specification expects for integer values.
 *  - The history backlog is pushed one record per work item instead of in a
 *    loop. That keeps the system work queue responsive and gives the stack
 *    natural backpressure on a slow link.
 *  - Subscribing to the history characteristic starts the replay. CCC state
 *    is per connection and is dropped when the central disconnects, so
 *    "subscribe and receive the backlog" is exactly what a phone sees after
 *    a reconnect.
 */

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "app_config.h"
#include "bootloader.h"
#include "env_service.h"
#include "history.h"
#include "power.h"

LOG_MODULE_REGISTER(env_service, LOG_LEVEL_INF);

/* ------------------------------------------------------------------------- */
/*  UUIDs                                                                     */
/* ------------------------------------------------------------------------- */

#define ENV_UUID_SVC  BT_UUID_128_ENCODE(0xe0f10001, 0x7b5a, 0x4b1c, 0x9a3d, 0x6f2c8e4d5a10)
#define ENV_UUID_TEMP BT_UUID_128_ENCODE(0xe0f10002, 0x7b5a, 0x4b1c, 0x9a3d, 0x6f2c8e4d5a10)
#define ENV_UUID_HUM  BT_UUID_128_ENCODE(0xe0f10003, 0x7b5a, 0x4b1c, 0x9a3d, 0x6f2c8e4d5a10)
#define ENV_UUID_BATT BT_UUID_128_ENCODE(0xe0f10004, 0x7b5a, 0x4b1c, 0x9a3d, 0x6f2c8e4d5a10)
#define ENV_UUID_HIST BT_UUID_128_ENCODE(0xe0f10005, 0x7b5a, 0x4b1c, 0x9a3d, 0x6f2c8e4d5a10)
#define ENV_UUID_CTRL BT_UUID_128_ENCODE(0xe0f10006, 0x7b5a, 0x4b1c, 0x9a3d, 0x6f2c8e4d5a10)

static struct bt_uuid_128 svc_uuid = BT_UUID_INIT_128(ENV_UUID_SVC);
static struct bt_uuid_128 temp_uuid = BT_UUID_INIT_128(ENV_UUID_TEMP);
static struct bt_uuid_128 hum_uuid = BT_UUID_INIT_128(ENV_UUID_HUM);
static struct bt_uuid_128 batt_uuid = BT_UUID_INIT_128(ENV_UUID_BATT);
static struct bt_uuid_128 hist_uuid = BT_UUID_INIT_128(ENV_UUID_HIST);
static struct bt_uuid_128 ctrl_uuid = BT_UUID_INIT_128(ENV_UUID_CTRL);

/* ------------------------------------------------------------------------- */
/*  State                                                                     */
/* ------------------------------------------------------------------------- */

static int16_t temp_value;   /* 0.01 degC */
static uint16_t hum_value;   /* 0.01 %RH  */
static uint16_t batt_value;  /* mV        */

static struct bt_conn *current_conn;
static bool history_subscribed;
static size_t replay_index;
static struct k_work_delayable replay_work;

/*
 * Attribute indices, in the same order as BT_GATT_SERVICE_DEFINE below.
 * Keeping them in an enum makes the notify calls readable and turns a
 * reordering mistake into an obvious, reviewable diff.
 */
enum env_attr_index {
	ENV_ATTR_SVC = 0,
	ENV_ATTR_TEMP_CHRC,
	ENV_ATTR_TEMP_VALUE,
	ENV_ATTR_TEMP_CCC,
	ENV_ATTR_HUM_CHRC,
	ENV_ATTR_HUM_VALUE,
	ENV_ATTR_HUM_CCC,
	ENV_ATTR_BATT_CHRC,
	ENV_ATTR_BATT_VALUE,
	ENV_ATTR_HIST_CHRC,
	ENV_ATTR_HIST_VALUE,
	ENV_ATTR_HIST_CCC,
	ENV_ATTR_CTRL_CHRC,
	ENV_ATTR_CTRL_VALUE,
	ENV_ATTR_COUNT,
};

/* ------------------------------------------------------------------------- */
/*  Attribute callbacks                                                       */
/* ------------------------------------------------------------------------- */

static ssize_t read_temp(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			 void *buf, uint16_t len, uint16_t offset)
{
	return bt_gatt_attr_read(conn, attr, buf, len, offset,
				 attr->user_data, sizeof(temp_value));
}

static ssize_t read_hum(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			void *buf, uint16_t len, uint16_t offset)
{
	return bt_gatt_attr_read(conn, attr, buf, len, offset,
				 attr->user_data, sizeof(hum_value));
}

static ssize_t read_batt(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			 void *buf, uint16_t len, uint16_t offset)
{
	return bt_gatt_attr_read(conn, attr, buf, len, offset,
				 attr->user_data, sizeof(batt_value));
}

static void temp_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);
	LOG_INF("temperature notifications %s",
		(value == BT_GATT_CCC_NOTIFY) ? "enabled" : "disabled");
}

static void hum_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);
	LOG_INF("humidity notifications %s",
		(value == BT_GATT_CCC_NOTIFY) ? "enabled" : "disabled");
}

static void replay_start(void);

static void hist_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);

	history_subscribed = (value == BT_GATT_CCC_NOTIFY);
	LOG_INF("history notifications %s", history_subscribed ? "enabled" : "disabled");

	if (history_subscribed) {
		/* A fresh subscription is the central asking for the backlog. */
		replay_start();
	}
}

static ssize_t write_ctrl(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			  const void *buf, uint16_t len, uint16_t offset,
			  uint8_t flags)
{
	const uint8_t *cmd = buf;

	ARG_UNUSED(conn);
	ARG_UNUSED(attr);
	ARG_UNUSED(flags);

	if (offset != 0U) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}
	if (len != 1U) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	switch (*cmd) {
	case ENV_CTRL_START_REPLAY:
		LOG_INF("history replay requested by central");
		replay_start();
		break;
	case ENV_CTRL_STOP_REPLAY:
		LOG_INF("history replay stopped by central");
		k_work_cancel_delayable(&replay_work);
		break;
	case ENV_CTRL_CLEAR_HISTORY:
		LOG_INF("history cleared by central");
		k_work_cancel_delayable(&replay_work);
		history_clear();
		break;
	case ENV_CTRL_ENTER_BOOTLOADER:
		LOG_INF("bootloader entry requested by central");
		/* Does not return: the device comes back up as a NICENANO
		 * mass-storage volume ready for a new .uf2.
		 */
		bootloader_request();
		break;
	default:
		LOG_WRN("unknown control command 0x%02x", *cmd);
		return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
	}

	return len;
}

/* ------------------------------------------------------------------------- */
/*  Service definition                                                        */
/* ------------------------------------------------------------------------- */

BT_GATT_SERVICE_DEFINE(env_svc,
	BT_GATT_PRIMARY_SERVICE(&svc_uuid),

	BT_GATT_CHARACTERISTIC(&temp_uuid.uuid,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ,
			       read_temp, NULL, &temp_value),
	BT_GATT_CCC(temp_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),

	BT_GATT_CHARACTERISTIC(&hum_uuid.uuid,
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ,
			       read_hum, NULL, &hum_value),
	BT_GATT_CCC(hum_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),

	BT_GATT_CHARACTERISTIC(&batt_uuid.uuid,
			       BT_GATT_CHRC_READ,
			       BT_GATT_PERM_READ,
			       read_batt, NULL, &batt_value),

	BT_GATT_CHARACTERISTIC(&hist_uuid.uuid,
			       BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_NONE,
			       NULL, NULL, NULL),
	BT_GATT_CCC(hist_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),

	BT_GATT_CHARACTERISTIC(&ctrl_uuid.uuid,
			       BT_GATT_CHRC_WRITE,
			       BT_GATT_PERM_WRITE,
			       NULL, write_ctrl, NULL),
);

/* ------------------------------------------------------------------------- */
/*  Advertising data                                                          */
/* ------------------------------------------------------------------------- */

static const struct bt_data adv_data[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
		sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static const struct bt_data adv_scan_rsp[] = {
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, ENV_UUID_SVC),
};

static void start_advertising(void)
{
	int err = bt_le_adv_start(BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONNECTABLE,
						  APP_ADV_INTERVAL_UNITS,
						  APP_ADV_INTERVAL_UNITS,
						  NULL),
				  adv_data, ARRAY_SIZE(adv_data),
				  adv_scan_rsp, ARRAY_SIZE(adv_scan_rsp));

	if (err != 0 && err != -EALREADY) {
		LOG_ERR("advertising start failed (%d)", err);
	}
}

/* ------------------------------------------------------------------------- */
/*  History replay                                                            */
/* ------------------------------------------------------------------------- */

/* One record per 10 ms finishes a full 100-record backlog in about a second. */
#define REPLAY_PACING_MS 10

static void replay_step(struct k_work *work)
{
	struct bt_conn *conn = current_conn;
	size_t total = history_count();
	const struct history_record *rec;
	int err;

	ARG_UNUSED(work);

	if (!history_subscribed || conn == NULL) {
		return;
	}

	if (replay_index >= total) {
		LOG_INF("history replay finished (%zu records, %u dropped)",
			total, history_dropped());
		history_clear();
		replay_index = 0U;
		return;
	}

	rec = history_at(replay_index);
	if (rec == NULL) {
		replay_index = 0U;
		return;
	}

	err = bt_gatt_notify(conn, &env_svc.attrs[ENV_ATTR_HIST_VALUE],
			     rec, sizeof(*rec));
	if (err < 0 && err != -ENOTCONN) {
		LOG_WRN("history notify %zu failed (%d)", replay_index, err);
	}

	replay_index++;
	k_work_reschedule(&replay_work, K_MSEC(REPLAY_PACING_MS));
}

static void replay_start(void)
{
	replay_index = 0U;

	if (history_count() == 0U) {
		LOG_INF("history empty, nothing to replay");
		return;
	}

	LOG_INF("replaying %zu buffered record(s)", history_count());
	k_work_reschedule(&replay_work, K_NO_WAIT);
}

/* ------------------------------------------------------------------------- */
/*  Connection handling                                                       */
/* ------------------------------------------------------------------------- */

static void request_conn_params(struct bt_conn *conn)
{
	/* 80 * 1.25 ms = 100 ms, the interval the specification asks for. */
	static const struct bt_le_conn_param param = {
		.interval_min = CONFIG_BT_PERIPHERAL_PREF_MIN_INT,
		.interval_max = CONFIG_BT_PERIPHERAL_PREF_MAX_INT,
		.latency = CONFIG_BT_PERIPHERAL_PREF_LATENCY,
		.timeout = CONFIG_BT_PERIPHERAL_PREF_TIMEOUT,
	};
	int err = bt_conn_le_param_update(conn, &param);

	if (err < 0 && err != -EALREADY) {
		LOG_WRN("connection parameter update failed (%d)", err);
	}
}

static void on_connected(struct bt_conn *conn, uint8_t err)
{
	if (err != 0U) {
		LOG_ERR("connection failed (0x%02x)", err);
		return;
	}

	current_conn = bt_conn_ref(conn);
	history_subscribed = false;
	LOG_INF("central connected");

	power_set_connected(true);
	request_conn_params(conn);

	/*
	 * The ATT MTU exchange is driven by the central: in Zephyr 3.6.99
	 * bt_gatt_exchange_mtu() takes an explicit params struct and is meant
	 * for the client role. nRF Connect issues it immediately after
	 * connecting, and the negotiated MTU shows up in the Bluetooth stack
	 * log (CONFIG_BT_ATT_MTU is the ceiling we advertise).
	 */
}

static void on_disconnected(struct bt_conn *conn, uint8_t reason)
{
	LOG_INF("central disconnected (reason 0x%02x), back to %d s sampling",
		reason, APP_OFFLINE_SAMPLE_INTERVAL_S);

	if (current_conn != NULL) {
		bt_conn_unref(current_conn);
		current_conn = NULL;
	}

	history_subscribed = false;
	k_work_cancel_delayable(&replay_work);

	/* This is where the device drops into its low power cadence. */
	power_set_connected(false);

	start_advertising();
}

static void on_le_param_updated(struct bt_conn *conn, uint16_t interval,
				uint16_t latency, uint16_t timeout)
{
	ARG_UNUSED(conn);
	LOG_INF("connection interval %.2f ms (latency %u, timeout %u ms)",
		(double)interval * 1.25, latency, timeout * 10U);
}

BT_CONN_CB_DEFINE(env_conn_callbacks) = {
	.connected = on_connected,
	.disconnected = on_disconnected,
	.le_param_updated = on_le_param_updated,
};

/* ------------------------------------------------------------------------- */
/*  Public API                                                                */
/* ------------------------------------------------------------------------- */

bool env_service_connected(void)
{
	return current_conn != NULL;
}

void env_service_publish(const struct env_sample *sample)
{
	struct bt_conn *conn = current_conn;

	if (sample == NULL) {
		return;
	}

	if (sample->temp_valid) {
		temp_value = sample->temp_c100;
	}
	if (sample->hum_valid) {
		hum_value = sample->hum_pct100;
	}
	if (sample->batt_valid) {
		batt_value = sample->batt_mv;
	}

	if (conn == NULL) {
		return; /* values stay readable, there is nobody to notify */
	}

	if (sample->temp_valid) {
		bt_gatt_notify(conn, &env_svc.attrs[ENV_ATTR_TEMP_VALUE],
			       &temp_value, sizeof(temp_value));
	}
	if (sample->hum_valid) {
		bt_gatt_notify(conn, &env_svc.attrs[ENV_ATTR_HUM_VALUE],
			       &hum_value, sizeof(hum_value));
	}
}

int env_service_init(void)
{
	k_work_init_delayable(&replay_work, replay_step);

	start_advertising();

	LOG_INF("advertising as \"%s\" (interval %u x 0.625 ms)",
		CONFIG_BT_DEVICE_NAME, APP_ADV_INTERVAL_UNITS);

	return 0;
}
