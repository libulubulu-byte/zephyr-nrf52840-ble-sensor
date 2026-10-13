/*
 * SHT31 (I2C, 0x44) + battery divider sampling.
 *
 * The SHT31 is driven through Zephyr's generic sensor API, so swapping it for
 * an SHTC3, SHT4x or a BME280 only changes the devicetree overlay - no code
 * here has to move.
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/dt-bindings/adc/nrf-adc.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "app_config.h"
#include "env_sensor.h"

LOG_MODULE_REGISTER(env_sensor, LOG_LEVEL_INF);

static const struct device *const sht = DEVICE_DT_GET(DT_NODELABEL(sht3xd));
static const struct device *const sht_bus = DEVICE_DT_GET(DT_BUS(DT_NODELABEL(sht3xd)));
static bool sht_ready;

#if APP_BATT_ADC_ENABLE
/*
 * One of two sense channels, chosen at build time - see
 * APP_BATT_SENSE_CHANNEL_IDX in app_config.h. ADC_DT_SPEC_GET() is only
 * shorthand for index 0, which is why this uses the by-index form: the board
 * has two dividers and the cheaper-looking choice is not always the right one.
 */
static const struct adc_dt_spec batt =
	ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), APP_BATT_SENSE_CHANNEL_IDX);
static int16_t batt_buf[APP_BATT_ADC_AVG];
#endif

/*
 * sensor_value stores a decimal: value = val1 + val2 / 1000000.
 * We want hundredths, so   hundredths = val1 * 100 + val2 / 10000.
 */
static int32_t to_centi(const struct sensor_value *v)
{
	return (int32_t)v->val1 * 100 + v->val2 / 10000;
}

/*
 * Sweep the I2C bus and report what answers.
 *
 * Zephyr's sht3xd driver performs no part-number check: sht3xd_init() only
 * verifies the bus is ready and then writes CLEAR_STATUS, failing with -EIO if
 * nobody acknowledges the address. That is good news for compatibility - an
 * SHT30, SHT31 and SHT35 are all the same thing as far as this driver is
 * concerned - but it also means "not ready" tells you nothing about *what* is
 * wrong.
 *
 * Sweeping turns that into an answer. Whichever address replies says whether
 * ADDR is floating (0x45 instead of 0x44), whether something else is on the
 * bus, or whether nothing is powered at all.
 */
static void i2c_scan(void)
{
	int found = 0;

	if (!device_is_ready(sht_bus)) {
		LOG_ERR("I2C bus %s is not ready", sht_bus->name);
		return;
	}

	LOG_WRN("sweeping %s for anything that acknowledges:", sht_bus->name);

	for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
		uint8_t scratch;

		/* Address-only read: a present device ACKs, whatever it
		 * drives back is discarded.
		 */
		if (i2c_read(sht_bus, &scratch, 1, addr) == 0) {
			LOG_WRN("  answered at 0x%02x", addr);
			found++;
		}
	}

	if (found == 0) {
		LOG_WRN("  nothing on the bus");
		LOG_WRN("  -> check 3V3 and GND first, then that SDA is on P1.04");
		LOG_WRN("     (silkscreen \"104\") and SCL on P1.06 (\"106\")");
	} else if (found == 1) {
		LOG_WRN("  -> one device found; if it is 0x45 then ADDR is");
		LOG_WRN("     floating and needs tying to GND");
	}
}

#if APP_BATT_ADC_ENABLE && APP_BATT_AIN_SWEEP_DIAG
/*
 * Bring-up aid: read every SAADC analog input and report the millivolts on
 * each, so the pin a divider is really wired to can be identified instead of
 * assumed.
 *
 * "Which pad is my divider on" has cost this project several rounds, because
 * the pin maps of the SuperMini, the Promicro and the nice!nano differ, and
 * the silkscreen does not always say which one you have. The nRF52840 has
 * eight analog inputs, all on port 0 (AIN0..AIN7 = P0.02, P0.03, P0.04, P0.05,
 * P0.28, P0.29, P0.30, P0.31). Reading them all takes a few milliseconds, once,
 * and turns the question into a reading.
 *
 * The driver selects ADC_CONFIGURABLE_INPUTS, so input_positive can be set at
 * runtime through adc_channel_setup().
 */
static const char *const ain_pin_name[8] = {
	"P0.02", "P0.03", "P0.04", "P0.05",
	"P0.28", "P0.29", "P0.30", "P0.31",
};

/* Millivolts at the pin for one analog input, using the battery channel's
 * gain, reference and acquisition time.
 */
static int sample_ain(uint8_t ain, int32_t *pin_mv)
{
	/*
	 * struct adc_dt_spec carries dev, channel_id, resolution and
	 * oversampling, but not the gain, reference or acquisition time - those
	 * exist only inside the channel config that adc_channel_setup_dt()
	 * builds. They are therefore repeated here and must stay in step with
	 * the channel node in
	 * boards/nordic/nrf52840_supermini/nrf52840_supermini.dts.
	 */
	struct adc_channel_cfg cfg = {
		.gain = ADC_GAIN_1_6,
		.reference = ADC_REF_INTERNAL,
		.acquisition_time = ADC_ACQ_TIME(ADC_ACQ_TIME_MICROSECONDS, 40),
		.channel_id = batt.channel_id,
		.differential = 0,
		.input_positive = NRF_SAADC_AIN0 + ain,
	};
	int16_t raw = 0;
	struct adc_sequence seq = {
		.buffer = &raw,
		.buffer_size = sizeof(raw),
	};
	int32_t mv;

	if (adc_channel_setup(batt.dev, &cfg) < 0) {
		return -EINVAL;
	}

	/* Sets .channels, .resolution and .oversampling; leaves the buffer
	 * alone, so the ones set above stand.
	 */
	if (adc_sequence_init_dt(&batt, &seq) < 0) {
		return -EINVAL;
	}

	if (adc_read_dt(&batt, &seq) < 0) {
		return -EIO;
	}

	mv = raw;
	if (adc_raw_to_millivolts_dt(&batt, &mv) < 0) {
		return -EINVAL;
	}

	*pin_mv = mv;
	return 0;
}

static void ain_sweep(void)
{
	LOG_INF("AIN sweep - looking for the divider (pin mV):");

	for (uint8_t ain = 0; ain < 8; ain++) {
		int32_t mv = 0;

		if (sample_ain(ain, &mv) == 0) {
			LOG_INF("  AIN%u %s = %d mV", ain, ain_pin_name[ain], mv);
		} else {
			LOG_INF("  AIN%u %s = <unavailable>", ain, ain_pin_name[ain]);
		}
	}

	/* Put the configured channel back so the sampler is unaffected. */
	(void)adc_channel_setup_dt(&batt);
}
#endif /* APP_BATT_ADC_ENABLE && APP_BATT_AIN_SWEEP_DIAG */

int env_sensor_init(void)
{
	int ret = 0;

	/*
	 * The battery divider and the SHT31 are independent, so the ADC is
	 * configured first and unconditionally. A missing or miswired sensor
	 * must not take the battery reading down with it - that is a bug this
	 * code had until it was run on real hardware, where an unwired SHT31
	 * produced "adc_nrfx_saadc: Channel 2 not configured" forever.
	 */
#if APP_BATT_ADC_ENABLE
	if (!adc_is_ready_dt(&batt)) {
		LOG_ERR("battery ADC not ready");
		ret = -ENODEV;
	} else {
		int err = adc_channel_setup_dt(&batt);

		if (err < 0) {
			LOG_ERR("adc_channel_setup failed (%d)", err);
			ret = err;
		} else {
			LOG_INF("battery ADC ready (%s, channel %d, divider %d/%d)",
				APP_BATT_SENSE_NAME, batt.channel_id,
				APP_BATT_DIVIDER_NUM, APP_BATT_DIVIDER_DEN);

#if APP_BATT_AIN_SWEEP_DIAG
			ain_sweep();
#endif
		}
	}
#endif /* APP_BATT_ADC_ENABLE */

	sht_ready = device_is_ready(sht);
	if (sht_ready) {
		LOG_INF("SHT3x ready (%s) at 0x%02x", sht->name,
			(uint8_t)DT_REG_ADDR(DT_NODELABEL(sht3xd)));
	} else {
		LOG_ERR("SHT3x not ready: no ACK from 0x%02x",
			(uint8_t)DT_REG_ADDR(DT_NODELABEL(sht3xd)));
		i2c_scan();
		if (ret == 0) {
			ret = -ENODEV;
		}
	}

	return ret;
}

static void read_temp_hum(struct env_sample *out)
{
	int err;

	/* Nothing to do, and no point logging a warning every 30 s, if the
	 * sensor did not answer at boot.
	 */
	if (!sht_ready) {
		return;
	}

	err = sensor_sample_fetch(sht);
	if (err < 0) {
		LOG_WRN("SHT3x fetch failed (%d)", err);
		return;
	}

	struct sensor_value sv;

	if (sensor_channel_get(sht, SENSOR_CHAN_AMBIENT_TEMP, &sv) == 0) {
		out->temp_c100 = (int16_t)to_centi(&sv);
		out->temp_valid = true;
	}

	if (sensor_channel_get(sht, SENSOR_CHAN_HUMIDITY, &sv) == 0) {
		out->hum_pct100 = (uint16_t)to_centi(&sv);
		out->hum_valid = true;
	}
}

static void read_battery(struct env_sample *out)
{
#if APP_BATT_ADC_ENABLE
	struct adc_sequence seq = {
		.buffer = batt_buf,
		.buffer_size = sizeof(batt_buf),
	};
	int32_t sum_mv = 0;
	int valid = 0;

	for (int i = 0; i < APP_BATT_ADC_AVG; i++) {
		int32_t raw;

		if (adc_sequence_init_dt(&batt, &seq) < 0) {
			return;
		}
		if (adc_read_dt(&batt, &seq) < 0) {
			return;
		}

		raw = batt_buf[0];
		if (adc_raw_to_millivolts_dt(&batt, &raw) < 0) {
			return;
		}

		sum_mv += raw;
		valid++;
	}

	if (valid > 0) {
		int32_t pin_mv = sum_mv / valid;
		int32_t mv = pin_mv * APP_BATT_DIVIDER_NUM / APP_BATT_DIVIDER_DEN;

		/*
		 * A cell cannot exceed ~4.2 V (4.35 V for the HV chemistries), so
		 * anything above that is a wiring fault rather than a battery.
		 *
		 * The first run of this code reported 65501 mV - a saturated value
		 * that had already been clamped to UINT16_MAX and told nobody
		 * anything. Naming the likely cause and the pin voltage turns the
		 * next occurrence into a diagnosis.
		 */
		if (mv > APP_BATT_PLAUSIBLE_MAX_MV) {
			LOG_WRN("battery reads %d mV (pin %d mV) - no cell can do "
				"that, so the sense pin is probably sitting on the "
				"supply rail. Check that the divider's lower leg "
				"actually reaches GND.",
				mv, pin_mv);
			return;
		}

		out->batt_mv = (uint16_t)mv;
		out->batt_valid = true;
	}
#endif /* APP_BATT_ADC_ENABLE */
}

int env_sensor_read(struct env_sample *out)
{
	if (out == NULL) {
		return -EINVAL;
	}

	*out = (struct env_sample){ 0 };

	read_temp_hum(out);
	read_battery(out);

	if (!out->temp_valid && !out->hum_valid && !out->batt_valid) {
		return -EIO;
	}

	return 0;
}
