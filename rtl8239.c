/* SPDX-License-Identifier: GPL-2.0-only */

#include "rtl8239.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "common.h"

#define I2C_SMBUS_READ 1
#define I2C_SMBUS_WRITE 0

#define RTL8239_COMM_COOLDOWN (25 * 1000)
#define MAX_READ_RETRY 40
#define U32_FIELD_SCALE(msb, lsb, scale) \
	((((uint32_t)msb) << 8 | ((uint32_t)lsb)) * scale)

static int
rtl8239_global_power_status_get(struct poemgr_pse_chip *pse_chip,
				struct rtl8239_system_power_status *p_status);

#if defined(ENABLE_DEBUG)
static const char *rtl8239_error_type_str(enum rtl8239_error_type type)
{
	switch (type) {
		ENUM_CASE2STR(OVLO);
		ENUM_CASE2STR(MPS_ABSENT);
		ENUM_CASE2STR(SHORT);
		ENUM_CASE2STR(OVERLOAD);
		ENUM_CASE2STR(POWER_DENIED);
		ENUM_CASE2STR(THERMAL_SHUTDOWN);
		ENUM_CASE2STR(INRUSH_FAIL);
		ENUM_CASE2STR(UVLO);
		ENUM_CASE2STR(GOTP);
		ENUM_CASE2STR(NO_ERROR);
	default:
		ENUM_CASE2STR(ERROR_TYPE_ERROR);
	}
}

static const char *
rtl8239_classification_result_str(enum rtl8239_classification_result result)
{
	switch (result) {
		ENUM_CASE2STR(PD0);
		ENUM_CASE2STR(PD1);
		ENUM_CASE2STR(PD2);
		ENUM_CASE2STR(PD3);
		ENUM_CASE2STR(PD4);
		ENUM_CASE2STR(PD5);
		ENUM_CASE2STR(PD6);
		ENUM_CASE2STR(PD7);
		ENUM_CASE2STR(PD8);
		ENUM_CASE2STR(PD_TREATED_AS_CLASS0);
		ENUM_CASE2STR(CLASS_MISMATCH);
		ENUM_CASE2STR(CLASS_OVERCURRENT);
	default:
		ENUM_CASE2STR(CLASSIFICATION_ERROR);
	}
}

static const char *rtl8239_detection_result_str(enum rtl8239_detection_result result)
{
	switch (result) {
		ENUM_CASE2STR(DETECTION_UNKNOWN);
		ENUM_CASE2STR(SHORT_CIRCUIT);
		ENUM_CASE2STR(HIGH_CAP);
		ENUM_CASE2STR(RLOW);
		ENUM_CASE2STR(VALID_PD);
		ENUM_CASE2STR(RHIGH);
		ENUM_CASE2STR(OPEN_CIRCUIT);
		ENUM_CASE2STR(FET_FAIL);
	default:
		ENUM_CASE2STR(DETECTION_ERROR);
	}
}

static const char *rtl8239_power_status_str(enum rtl8239_power_status status)
{
	switch (status) {
		ENUM_CASE2STR(DISABLED);
		ENUM_CASE2STR(SEARCHING);
		ENUM_CASE2STR(DELIVERING_POWER);
		ENUM_CASE2STR(FAULT);
		ENUM_CASE2STR(REQUESTING_POWER);
	default:
		ENUM_CASE2STR(POWER_STATUS_ERROR);
	}
}

static const char *
rtl8239_connection_check_result_str(enum rtl8239_connection_check_result result)
{
	switch (result) {
		ENUM_CASE2STR(TWO_PAIR);
		ENUM_CASE2STR(SINGLE_PD);
		ENUM_CASE2STR(DUAL_PD);
		ENUM_CASE2STR(CONNECTION_CHECK_UNKNOWN);
	default:
		ENUM_CASE2STR(CONNECTION_CHECK_ERROR);
	}
}
#endif

static const char *rtl8239_device_id_str(enum rtl8239_device_id device_id)
{
	switch (device_id) {
		ENUM_CASE2STR(RTL8238B);
		ENUM_CASE2STR(RTL8238C);
		ENUM_CASE2STR(RTL8239);
		ENUM_CASE2STR(RTL8239C);
	default:
		ENUM_CASE2STR(DEVICE_ID_UNKNOWN);
	}
}

static struct rtl8239_priv *rtl8239_priv(struct poemgr_pse_chip *pse_chip)
{
	return (struct rtl8239_priv *)pse_chip->priv;
}

static int rtl8239_wr(struct poemgr_pse_chip *pse_chip, uint8_t reg,
		      uint8_t *val, uint8_t len)
{
	struct rtl8239_priv *priv = rtl8239_priv(pse_chip);
	union i2c_smbus_data data = { 0 };
	if (len > I2C_SMBUS_BLOCK_MAX)
		return -EINVAL;

	data.block[0] = len;
	memcpy(&data.block[1], val, len);

	return i2c_smbus_access(priv->i2c_fd, I2C_SMBUS_WRITE, reg,
				I2C_SMBUS_I2C_BLOCK_DATA, &data);
}

/**
 * Returns number of bytes read with I2C_BLOCK_DATA
 */
static int rtl8239_rr(struct poemgr_pse_chip *pse_chip, uint8_t reg,
		      uint8_t len, uint8_t *ret_buff)
{
	int ret_len = 0;
	struct rtl8239_priv *priv = rtl8239_priv(pse_chip);
	union i2c_smbus_data data = { 0 };

	data.block[0] = len;

	if (i2c_smbus_access(priv->i2c_fd, I2C_SMBUS_READ, reg,
			     I2C_SMBUS_I2C_BLOCK_DATA, &data))
		return -1;

	// Return number of bytes read
	ret_len = (len >= data.block[0]) ? data.block[0] : len;
	// response length is data.block[0]
	// response is &data.block[1]
	memcpy(ret_buff, &data.block[1], ret_len);

	return ret_len;
}

#define MAX_PARAMS 9
#define MAX_RESP 12
struct request {
	uint8_t cmd;
	int seq;
	union {
		uint8_t params[MAX_PARAMS];
		uint8_t response[MAX_RESP];
	};
	int chksm;
};

static int _rtl8239_request_response(struct poemgr_pse_chip *pse_chip,
				     struct request *r, int validate)
{
	int ret = -1;
	int i, j;
	uint8_t data_block[MAX_RESP - 1] = { 0 };
	uint8_t response_block[MAX_RESP] = { 0 };
	int actual_response_len;
	int data_len = MAX_RESP - 1;
	int expected_response_len = MAX_RESP;
	static uint8_t seq_counter = 0;

	if (!r) {
		ret = -ENOMEM;
		goto out;
	}

	if (r->seq == -1)
		data_block[0] = ++seq_counter;
	else
		data_block[0] = (r->seq & 0xff);

	memcpy(&data_block[1], r->params, MAX_PARAMS);

	if (r->chksm == -1) {
		data_block[10] = r->cmd;
		for (i = 0; i < data_len - 1; i++)
			data_block[10] = (data_block[10] + data_block[i]) &
					 0xff;
	} else
		data_block[10] = (r->chksm & 0xff);

	ret = rtl8239_wr(pse_chip, r->cmd, data_block, data_len);
	if (ret < 0) {
		perror("rtl8239_wr");
		goto out;
	}

	for (i = 0; i < MAX_READ_RETRY; i++) {
		usleep(RTL8239_COMM_COOLDOWN);

		actual_response_len = rtl8239_rr(
			pse_chip, 0x00, expected_response_len, response_block);

		// Check resp length
		ret = (expected_response_len == actual_response_len);
		if (!ret) {
			debug("Less number of bytes than expected. expected:%02x != got:%02x\n",
			      expected_response_len, actual_response_len)
				perror("Less number of bytes than expected received, retrying.");
			continue;
		}

		if (validate) {
			// Check CMD / RESP id
			ret = (response_block[0] == r->cmd);
			if (!ret) {
				debug("CMD_ID and RESP_ID are differet. expected:%02x != got:%02x\n",
				      r->cmd, response_block[0]);
				perror("CMD_ID and RESP_ID are different, out of sync. retrying.");
				continue;
			}

			// validate sequence
			ret = (response_block[1] == data_block[0]);
			if (!ret) {
				debug("seq does not match, expected:%02x != got:%02x\n",
				      data_block[0], response_block[1]);
				perror("SEQ does not match, out of sync, retrying.");
				continue;
			}

			// Calculate chksm
			r->chksm = 0;
			for (j = 0; j < expected_response_len - 1; j++)
				r->chksm = (r->chksm + response_block[j]) &
					   0xff;

			// Validate Chksm
			ret = (r->chksm == response_block[11]);
			if (!ret) {
				debug("Checksum does not match expected:%02x != got:%02x. i2c comm error. fix the driver.\n",
				      r->chksm, response_block[11]);
				perror("i2c comm error. fix the driver.");
				continue;
			}
		}

		if (ret)
			break;
	}

	if (!ret)
		goto out;

	memcpy(r->response, &response_block[0], MAX_RESP);

	ret = 0;
out:
	usleep(RTL8239_COMM_COOLDOWN);
	return ret;
}

static int rtl8239_request_response(struct poemgr_pse_chip *pse_chip,
				    struct request *r)
{
	return _rtl8239_request_response(pse_chip, r, 1);
}
static int rtl8239_request(struct poemgr_pse_chip *pse_chip, struct request *r)
{
	return _rtl8239_request_response(pse_chip, r, 0);
}

int rtl8239_global_enable(struct poemgr_pse_chip *pse_chip, int enable_disable)
{
	int ret = -1;
	struct request global_enable_set = {
		.cmd = 0x0,
		.seq = -1,
		.chksm = -1,
	};

	memset(global_enable_set.params, 0xff, MAX_PARAMS);

	global_enable_set.params[0] = !!enable_disable;
	if ((ret = rtl8239_request_response(pse_chip, &global_enable_set))) {
		perror("rtl8239_request_response(global_enable_set): ");
		goto out;
	}
	if (global_enable_set.response[2] != 0) {
		debug("Global enable failed\n");
		ret = -2;
		goto out;
	} else
		debug("Global enable success\n");

	ret = 0;
out:
	// datasheet mentions command 0x0 takes considerably longer than other commands
	usleep(3 * 1000 * 1000);
	return ret;
}

int rtl8239_port_max_power_type_set(struct poemgr_pse_chip *pse_chip, int port,
				    enum rtl8239_port_max_power_type power_type)
{
	int ret = -1;
	struct request port_max_power_type_set = {
		.cmd = 0x12,
		.seq = -1,
		.chksm = -1,
	};

	memset(port_max_power_type_set.params, 0xff, MAX_PARAMS);
	port_max_power_type_set.params[0] = port;
	port_max_power_type_set.params[1] = power_type;

	if ((ret = rtl8239_request_response(pse_chip,
					    &port_max_power_type_set))) {
		perror("rtl8239_request_response(port_max_power_type_set): ");
		goto out;
	}

	if (port_max_power_type_set.response[3] != 0) {
		debug("Port max power type set\n");
		goto out;
	}

	ret = 0;
out:
	return ret;
}

int rtl8239_global_power_source_set(struct poemgr_pse_chip *pse_chip,
				    uint32_t power_budget_watts)
{
	int ret = -1;
	struct rtl8239_system_power_status power_status = { 0 };
	struct request global_power_source_set = {
		.cmd = 0x04,
		.seq = -1,
		.chksm = -1,
	};

	memset(global_power_source_set.params, 0xff, MAX_PARAMS);

	rtl8239_global_power_status_get(pse_chip, &power_status);

	// Use Bank id used by the system
	global_power_source_set.params[0] = power_status.bank_id;
	/* 0.1 Watt / LSB */
	power_budget_watts *= 10;

	global_power_source_set.params[1] = power_budget_watts >> 8;
	global_power_source_set.params[2] = power_budget_watts & 0xff;
	// TODO: How much to reserve? What is reserved power?
	global_power_source_set.params[3] = 0x00;
	global_power_source_set.params[4] = 0x00;

	if ((ret = rtl8239_request_response(pse_chip,
					    &global_power_source_set))) {
		perror("rtl8239_request_response(global_power_source_set): ");
		goto out;
	}

	if (global_power_source_set.response[3] != 0) {
		debug("Global power source set failed\n");
		goto out;
	}

	ret = 0;
out:
	return ret;
}

int rtl8239_global_power_management_conf_get(
	struct poemgr_pse_chip *pse_chip,
	struct rtl8239_system_power_status *p_status)
{
	int ret = -1;
	struct request global_power_management_conf_get = {
		.cmd = 0x4b,
		// .seq = -1,
		.params = { 0 },
		.chksm = -1,
	};

	if (!p_status) {
		ret = -ENOMEM;
		goto out;
	}

	// HACK: Datasheet shows using seq field to supply bank_id parameter
	global_power_management_conf_get.seq = p_status->bank_id;

	ret = rtl8239_request_response(pse_chip,
				       &global_power_management_conf_get);
	if (ret) {
		perror("rtl8239_request_response(global_power_management_conf_get): ");
		goto out;
	}

	if (p_status->bank_id != global_power_management_conf_get.response[1]) {
		debug("Response bank_id does not match, expected:%02x != got:%02x\n",
		      p_status->bank_id,
		      global_power_management_conf_get.response[1]);
		goto out;
	}

	p_status->bank_mgmt_mode = global_power_management_conf_get.response[2];
	p_status->bank_total_power = U32_FIELD_SCALE(
		global_power_management_conf_get.response[3],
		global_power_management_conf_get.response[4], 100);
	p_status->bank_reserved_power = U32_FIELD_SCALE(
		global_power_management_conf_get.response[5],
		global_power_management_conf_get.response[6], 100);

	ret = 0;
out:
	return ret;
}

int rtl8239_global_power_management_mode_set(
	struct poemgr_pse_chip *pse_chip,
	struct rtl8239_system_power_status status)
{
	int ret = -1;
	struct request global_power_management_mode_set = {
		.cmd = 0x10,
		.seq = -1,
		.chksm = -1,
	};

	struct request global_power_management_mode_extended_set = {
		.cmd = 0x11,
		.seq = -1,
		.chksm = -1,
	};

	memset(global_power_management_mode_set.params, 0xff, MAX_PARAMS);
	memset(global_power_management_mode_extended_set.params, 0xff,
	       MAX_PARAMS);

	global_power_management_mode_set.params[0] = status.bank_mgmt_mode;
	if ((ret = rtl8239_request_response(
		     pse_chip, &global_power_management_mode_set))) {
		perror("rtl8239_request_response(global_power_management_mode_set): ");
		goto out;
	}
	if (global_power_management_mode_set.response[2] != 0) {
		debug("Global power management mode set failed\n");
		goto out;
	}

	global_power_management_mode_extended_set.params[0] =
		status.bank_disable_prealloc;
	if ((ret = rtl8239_request_response(
		     pse_chip, &global_power_management_mode_extended_set))) {
		perror("rtl8239_request_response(global_power_management_mode_extended_set): ");
		goto out;
	}

	if (global_power_management_mode_extended_set.response[2] != 0) {
		debug("Global power management mode extended set failed\n");
		goto out;
	}

	ret = 0;

out:
	return ret;
}

int rtl8239_device_reset(struct poemgr_pse_chip *pse_chip)
{
	int ret = -1;
	struct request global_reset_set = {
		.cmd = 0x02,
		.seq = -1,
		.chksm = -1,
	};

	memset(global_reset_set.params, 0xff, MAX_PARAMS);

	/* Reset chip */
	global_reset_set.params[0] = 0x1;
	if (rtl8239_request(pse_chip, &global_reset_set)) {
		perror("rtl8239_request(global_reset_set): ");
		ret = -4;
		goto out;
	}

	ret = 0;
out:
	// Although datasheet doesn't mention waiting this long
	// this command still causes problems some times
	usleep(3 * 1000 * 1000);
	return ret;
}

int rtl8239_port_event_status_get(struct poemgr_pse_chip *pse_chip)
{
	int ret = -1;
	struct request port_event_status_get = {
		.cmd = 0x46,
		.seq = -1,
		.params = { 0 },
		.chksm = -1,
	};

	ret = rtl8239_request_response(pse_chip, &port_event_status_get);
	if (ret) {
		perror("rtl8239_request_response(port_event_status_get): ");
		goto out;
	}

#if defined(ENABLE_DEBUG)
	debug("Event mask: discon_evt_msk: %d faults_evt_msk: %d\n",
	      !!(port_event_status_get.response[2] & BIT(1)),
	      !!(port_event_status_get.response[2] & BIT(2)));
	debug("Event status: discon_evt_sts: %d fault_evt_sts: %d\n",
	      !!(port_event_status_get.response[3] & BIT(1)),
	      !!(port_event_status_get.response[3] & BIT(2)));

	debug("Port 0 event: %d\n",
	      !!(port_event_status_get.response[4] & BIT(0)));
	debug("Port 1 event: %d\n",
	      !!(port_event_status_get.response[4] & BIT(1)));
	debug("Port 2 event: %d\n",
	      !!(port_event_status_get.response[4] & BIT(2)));
	debug("Port 3 event: %d\n",
	      !!(port_event_status_get.response[4] & BIT(3)));
	debug("Port 4 event: %d\n",
	      !!(port_event_status_get.response[4] & BIT(4)));
	debug("Port 5 event: %d\n",
	      !!(port_event_status_get.response[4] & BIT(5)));
	debug("Port 6 event: %d\n",
	      !!(port_event_status_get.response[4] & BIT(6)));
	debug("Port 7 event: %d\n",
	      !!(port_event_status_get.response[4] & BIT(7)));

	debug("Port 8 event: %d\n",
	      !!(port_event_status_get.response[5] & BIT(0)));
	debug("Port 9 event: %d\n",
	      !!(port_event_status_get.response[5] & BIT(1)));
	debug("Port 10 event: %d\n",
	      !!(port_event_status_get.response[5] & BIT(2)));
	debug("Port 11 event: %d\n",
	      !!(port_event_status_get.response[5] & BIT(3)));
	debug("Port 12 event: %d\n",
	      !!(port_event_status_get.response[5] & BIT(4)));
	debug("Port 13 event: %d\n",
	      !!(port_event_status_get.response[5] & BIT(5)));
	debug("Port 14 event: %d\n",
	      !!(port_event_status_get.response[5] & BIT(6)));
	debug("Port 15 event: %d\n",
	      !!(port_event_status_get.response[5] & BIT(7)));

	debug("Port 16 event: %d\n",
	      !!(port_event_status_get.response[6] & BIT(0)));
	debug("Port 17 event: %d\n",
	      !!(port_event_status_get.response[6] & BIT(1)));
	debug("Port 18 event: %d\n",
	      !!(port_event_status_get.response[6] & BIT(2)));
	debug("Port 19 event: %d\n",
	      !!(port_event_status_get.response[6] & BIT(3)));
	debug("Port 20 event: %d\n",
	      !!(port_event_status_get.response[6] & BIT(4)));
	debug("Port 21 event: %d\n",
	      !!(port_event_status_get.response[6] & BIT(5)));
	debug("Port 22 event: %d\n",
	      !!(port_event_status_get.response[6] & BIT(6)));
	debug("Port 23 event: %d\n",
	      !!(port_event_status_get.response[6] & BIT(7)));

	debug("Port 24 event: %d\n",
	      !!(port_event_status_get.response[7] & BIT(0)));
	debug("Port 25 event: %d\n",
	      !!(port_event_status_get.response[7] & BIT(1)));
	debug("Port 26 event: %d\n",
	      !!(port_event_status_get.response[7] & BIT(2)));
	debug("Port 27 event: %d\n",
	      !!(port_event_status_get.response[7] & BIT(3)));
	debug("Port 28 event: %d\n",
	      !!(port_event_status_get.response[7] & BIT(4)));
	debug("Port 29 event: %d\n",
	      !!(port_event_status_get.response[7] & BIT(5)));
	debug("Port 30 event: %d\n",
	      !!(port_event_status_get.response[7] & BIT(6)));
	debug("Port 31 event: %d\n",
	      !!(port_event_status_get.response[7] & BIT(7)));

	debug("Port 32 event: %d\n",
	      !!(port_event_status_get.response[8] & BIT(0)));
	debug("Port 33 event: %d\n",
	      !!(port_event_status_get.response[8] & BIT(1)));
	debug("Port 34 event: %d\n",
	      !!(port_event_status_get.response[8] & BIT(2)));
	debug("Port 35 event: %d\n",
	      !!(port_event_status_get.response[8] & BIT(3)));
	debug("Port 36 event: %d\n",
	      !!(port_event_status_get.response[8] & BIT(4)));
	debug("Port 37 event: %d\n",
	      !!(port_event_status_get.response[8] & BIT(5)));
	debug("Port 38 event: %d\n",
	      !!(port_event_status_get.response[8] & BIT(6)));
	debug("Port 39 event: %d\n",
	      !!(port_event_status_get.response[8] & BIT(7)));

	debug("Port 40 event: %d\n",
	      !!(port_event_status_get.response[9] & BIT(0)));
	debug("Port 41 event: %d\n",
	      !!(port_event_status_get.response[9] & BIT(1)));
	debug("Port 42 event: %d\n",
	      !!(port_event_status_get.response[9] & BIT(2)));
	debug("Port 43 event: %d\n",
	      !!(port_event_status_get.response[9] & BIT(3)));
	debug("Port 44 event: %d\n",
	      !!(port_event_status_get.response[9] & BIT(4)));
	debug("Port 45 event: %d\n",
	      !!(port_event_status_get.response[9] & BIT(5)));
	debug("Port 46 event: %d\n",
	      !!(port_event_status_get.response[9] & BIT(6)));
	debug("Port 47 event: %d\n",
	      !!(port_event_status_get.response[9] & BIT(7)));

#endif
out:
	return ret;
}

static int rtl8239_global_status_get(struct poemgr_pse_chip *pse_chip,
				     struct rtl8239_system_status *p_status)
{
	int ret = -1;
	struct request global_status_get = {
		.cmd = 0x40,
		.seq = -1,
		.params = { 0 },
		.chksm = -1,
	};

	if (!p_status) {
		ret = -ENOMEM;
		goto out;
	}

	ret = rtl8239_request_response(pse_chip, &global_status_get);
	if (ret) {
		perror("rtl8239_request_response(global_status_get): ");
		goto out;
	}

	p_status->max_ports = global_status_get.response[3];
	p_status->poe_enabled = !!global_status_get.response[4];
	p_status->device_id = global_status_get.response[5] << 8 |
			      global_status_get.response[6];
	if (p_status->device_id != RTL8238B &&
	    p_status->device_id != RTL8238C && p_status->device_id != RTL8239 &&
	    p_status->device_id != RTL8239C) {
		p_status->device_id = DEVICE_ID_UNKNOWN;
	}

	p_status->sw_version = global_status_get.response[7];
	snprintf(p_status->sw_version_str, sizeof(p_status->sw_version_str),
		 "%02x (%us.%us)", p_status->sw_version,
		 p_status->sw_version >> 4, p_status->sw_version & 0xf);

	p_status->mcu_type = global_status_get.response[8];
	switch (p_status->mcu_type) {
	case 0x00:
		snprintf(p_status->mcu_type_str, sizeof(p_status->mcu_type_str),
			 "GigaDevice GD32F310XXXX");
		break;
	case 0x01:
		snprintf(p_status->mcu_type_str, sizeof(p_status->mcu_type_str),
			 "GigaDevice GD32E230XXXX");
		break;
	case 0x02:
		snprintf(p_status->mcu_type_str, sizeof(p_status->mcu_type_str),
			 "GigaDevice GD32F303XXXX");
		break;
	case 0x03:
		snprintf(p_status->mcu_type_str, sizeof(p_status->mcu_type_str),
			 "GigaDevice GD32F103XXXX");
		break;
	case 0x04:
		snprintf(p_status->mcu_type_str, sizeof(p_status->mcu_type_str),
			 "GigaDevice GD32E103XXXX");
		break;
	case 0x10:
		snprintf(p_status->mcu_type_str, sizeof(p_status->mcu_type_str),
			 "Nuvoton M0516XXXX");
		break;
	case 0x11:
		snprintf(p_status->mcu_type_str, sizeof(p_status->mcu_type_str),
			 "Nuvoton M0564XXXX");
		break;
	case 0x12:
		snprintf(p_status->mcu_type_str, sizeof(p_status->mcu_type_str),
			 "Nuvoton NUC029XXXX");
		break;
	default:
		snprintf(p_status->mcu_type_str, sizeof(p_status->mcu_type_str),
			 "MCU_TYPE_UNKNOWN");
		break;
	}

	p_status->config_is_dirty = global_status_get.response[9] & 0b1;
	p_status->system_reset_happened = global_status_get.response[9] & 0b10;
	p_status->global_disable_low = global_status_get.response[9] & 0b100;

	p_status->ext_ver = global_status_get.response[10];
	snprintf(p_status->ext_ver_str, sizeof(p_status->ext_ver_str),
		 "%02x (%us.%us)", p_status->ext_ver, p_status->ext_ver >> 4,
		 p_status->ext_ver & 0xf);

#if defined(ENABLE_DEBUG)
	debug("Max Ports: %d\n", p_status->max_ports);
	debug("is POE system enabled? %s\n",
	      p_status->poe_enabled ? "YES" : "NO");
	debug("device id: %04x (%s)\n", p_status->device_id,
	      rtl8239_device_id_str(p_status->device_id));
	debug("sw_version: %02x (%s)\n", p_status->sw_version,
	      p_status->sw_version_str);
	debug("mcu_type: %02x (%s)\n", p_status->mcu_type,
	      p_status->mcu_type_str);
	debug("is config dirty? %s\n",
	      p_status->config_is_dirty ? "YES" : "NO");
	debug("did system reset happen? %s\n",
	      p_status->system_reset_happened ? "YES" : "NO");
	debug("is global disable pin low? %s\n",
	      p_status->global_disable_low ? "YES" : "NO");
	debug("ext version: %02x (%s)\n", p_status->ext_ver,
	      p_status->ext_ver_str);
#endif

	ret = 0;
out:
	return ret;
}

int rtl8239_device_online(struct poemgr_pse_chip *pse_chip)
{
	int ret = -1;
	struct rtl8239_system_status status = { 0 };

	ret = rtl8239_global_status_get(pse_chip, &status);

	if (ret) {
		perror("rtl8239_global_status_get(): ");
		goto out;
	}

	if (status.device_id == DEVICE_ID_UNKNOWN) {
		ret = -3;
		goto out;
	}

	ret = 0;
out:
	return ret;
}

int rtl8239_device_enable(struct poemgr_pse_chip *pse_chip)
{
	// TODO
	return 0;
}

static int
rtl8239_global_power_status_get(struct poemgr_pse_chip *pse_chip,
				struct rtl8239_system_power_status *p_status)
{
	int ret = -1;
	struct request global_power_status_get = {
		.cmd = 0x41,
		.seq = -1,
		.params = { 0 },
		.chksm = -1,
	};

	if (!p_status) {
		ret = -ENOMEM;
		goto out;
	}

	if (rtl8239_request_response(pse_chip, &global_power_status_get)) {
		perror("rtl8239_request_response(global_power_status_get): ");
		ret = -1;
		goto out;
	}

	p_status->allocated_power =
		U32_FIELD_SCALE(global_power_status_get.response[2],
				global_power_status_get.response[3], 100);
	p_status->available_power =
		U32_FIELD_SCALE(global_power_status_get.response[4],
				global_power_status_get.response[5], 100);
	p_status->current_power =
		U32_FIELD_SCALE(global_power_status_get.response[7],
				global_power_status_get.response[8], 100);

	p_status->bank_id = global_power_status_get.response[6];

#if defined(ENABLE_DEBUG)
	debug("Allocated Power: %02x %02x (%d mW)\n",
	      global_power_status_get.response[2],
	      global_power_status_get.response[3], p_status->allocated_power);
	debug("Available Power: %02x %02x (%d mW)\n",
	      global_power_status_get.response[4],
	      global_power_status_get.response[5], p_status->available_power);
	debug("Current Power: %02x %02x (%d mW)\n",
	      global_power_status_get.response[7],
	      global_power_status_get.response[8], p_status->current_power);
#endif
	ret = 0;
	// TODO:
out:
	return ret;
}

static inline enum rtl8239_classification_result
rtl8239_parse_classification_result(uint8_t classification_word)
{
	enum rtl8239_classification_result classification_result;

	if (classification_word >= PD0 && classification_word <= PD8)
		classification_result = classification_word;
	else {
		switch (classification_word) {
		case 0xc:
			classification_result = PD_TREATED_AS_CLASS0;
			break;
		case 0xe:
			classification_result = CLASS_MISMATCH;
			break;
		case 0xf:
			classification_result = CLASS_OVERCURRENT;
			break;
		default:
			classification_result = CLASSIFICATION_ERROR;
			break;
		}
	}

	return classification_result;
}

int rtl8239_port_status_get(struct poemgr_pse_chip *pse_chip,
			    struct rtl8239_port_status *p_status)
{
	int ret = -1;
	struct request port_status_get = {
		.cmd = 0x42,
		.seq = -1,
		.params = { 0 },
		.chksm = -1,
	};

	if (!p_status) {
		ret = -ENOMEM;
		goto out;
	}

	port_status_get.params[0] = p_status->port;

	if (rtl8239_request_response(pse_chip, &port_status_get)) {
		perror("rtl8239_request_response(port_sttaus_get): ");
		ret = -1;
		goto out;
	}

	if (p_status->port != port_status_get.response[2]) {
		debug("port and response does not match, expected:%02x != got:%02x\n",
		      p_status->port, port_status_get.response[2]);
		ret = -2;
		goto out;
	}

	switch (port_status_get.response[3]) {
	case 0x0:
		p_status->power_status = DISABLED;
		break;
	case 0x1:
		p_status->power_status = SEARCHING;
		break;
	case 0x2:
		p_status->power_status = DELIVERING_POWER;
		break;
	case 0x4:
		p_status->power_status = FAULT;
		break;
	case 0x6:
		p_status->power_status = REQUESTING_POWER;
		break;
	case 0x3:
	case 0x5:
	default:
		p_status->power_status = POWER_STATUS_ERROR;
		break;
	}

	if (p_status->power_status == FAULT ||
	    p_status->power_status == POWER_STATUS_ERROR) {
		switch (port_status_get.response[4]) {
		case 0x0:
			p_status->error_type = OVLO;
			break;
		case 0x1:
			p_status->error_type = MPS_ABSENT;
			break;
		case 0x2:
			p_status->error_type = SHORT;
			break;
		case 0x3:
			p_status->error_type = OVERLOAD;
			break;
		case 0x4:
			p_status->error_type = POWER_DENIED;
			break;
		case 0x5:
			p_status->error_type = THERMAL_SHUTDOWN;
			break;
		case 0x6:
			p_status->error_type = INRUSH_FAIL;
			break;
		case 0x7:
			p_status->error_type = UVLO;
			break;
		case 0xE:
			p_status->error_type = GOTP;
			break;
		default:
			p_status->error_type = ERROR_TYPE_ERROR;
			break;
		}
		/* In case of PoE fault, detection and classification results are not provided */
		p_status->detection_result = DETECTION_ERROR;
		p_status->classification_result = CLASSIFICATION_ERROR;
	} else {
		/* In case of no fault, error to no error */
		p_status->error_type = NO_ERROR;
		switch (port_status_get.response[4] & 0x0f) {
		case 0x0:
			p_status->detection_result = DETECTION_UNKNOWN;
			break;
		case 0x1:
			p_status->detection_result = SHORT_CIRCUIT;
			break;
		case 0x2:
			p_status->detection_result = HIGH_CAP;
			break;
		case 0x3:
			p_status->detection_result = RLOW;
			break;
		case 0x4:
			p_status->detection_result = VALID_PD;
			break;
		case 0x5:
			p_status->detection_result = RHIGH;
			break;
		case 0x6:
			p_status->detection_result = OPEN_CIRCUIT;
			break;
		case 0x7:
			p_status->detection_result = FET_FAIL;
			break;
		default:
			p_status->detection_result = DETECTION_ERROR;
			break;
		}

		// BIT(7-4)
		p_status->classification_result =
			rtl8239_parse_classification_result(
				port_status_get.response[4] >> 4);
	}

	switch (port_status_get.response[7]) {
	case 0x0:
		p_status->connection_check_result = TWO_PAIR;
		break;
	case 0x1:
		p_status->connection_check_result = SINGLE_PD;
		break;
	case 0x2:
		p_status->connection_check_result = DUAL_PD;
		break;
	case 0x3:
		p_status->connection_check_result = CONNECTION_CHECK_UNKNOWN;
		break;
	default:
		p_status->connection_check_result = CONNECTION_CHECK_ERROR;
		break;
	}

	if (p_status->connection_check_result == DUAL_PD) {
		p_status->secondary_channel_classification_result =
			rtl8239_parse_classification_result(
				port_status_get.response[5] >> 4);
		p_status->primary_channel_classification_result =
			rtl8239_parse_classification_result(
				port_status_get.response[5] & 0xf);
	} else {
		p_status->valid_channel_classification_result =
			rtl8239_parse_classification_result(
				port_status_get.response[5]);
	}

#if defined(ENABLE_DEBUG)
	debug("port:%d power_status: %s error_type: %s detection_result: %s classification_result: %s primary_channel_classification: %s secondary_channel_classification: %s valid_channel_classification_result: %s connection_check_result: %s\n",
	      p_status->port, rtl8239_power_status_str(p_status->power_status),
	      rtl8239_error_type_str(p_status->error_type),
	      rtl8239_detection_result_str(p_status->detection_result),
	      rtl8239_classification_result_str(
		      p_status->classification_result),
	      rtl8239_classification_result_str(
		      p_status->primary_channel_classification_result),
	      rtl8239_classification_result_str(
		      p_status->secondary_channel_classification_result),
	      rtl8239_classification_result_str(
		      p_status->valid_channel_classification_result),
	      rtl8239_connection_check_result_str(
		      p_status->connection_check_result));
#endif

	ret = 0;
out:
	return ret;
}

int rtl8239_port_measurement_get(struct poemgr_pse_chip *pse_chip,
				 struct rtl8239_port_measurement *p_measurement)
{
	int ret = -1;
	struct request port_measurement_get = {
		.cmd = 0x44,
		.seq = -1,
		.params = { 0 },
		.chksm = -1,
	};

	if (!p_measurement) {
		ret = -ENOMEM;
		goto out;
	}

	port_measurement_get.params[0] = p_measurement->port;
	if (rtl8239_request_response(pse_chip, &port_measurement_get)) {
		perror("rtl8239_request_response(port_measurement_get): ");
		ret = -1;
		goto out;
	}

	if (p_measurement->port != port_measurement_get.response[2]) {
		debug("port and response port does not match. expected:%02x != got:%02x\n",
		      p_measurement->port, port_measurement_get.response[2]);
		ret = -2;
		goto out;
	}

	// milliVolts
	p_measurement->voltage =
		U32_FIELD_SCALE(port_measurement_get.response[3],
				port_measurement_get.response[4], 64.45);
	// milliamps
	p_measurement->current =
		U32_FIELD_SCALE(port_measurement_get.response[5],
				port_measurement_get.response[6], 1);
	// celsius
	p_measurement->temperature =
		(U32_FIELD_SCALE(port_measurement_get.response[7],
				 port_measurement_get.response[8], 1) -
		 120) * (-1.25) +
		125.00;
	// milliwatts
	p_measurement->power =
		U32_FIELD_SCALE(port_measurement_get.response[9],
				port_measurement_get.response[10], 100);

#if defined(ENABLE_DEBUG)
	hexdump(port_measurement_get.response, MAX_RESP);
	debug("port:%d voltage: %u current: %u temperature: %u power: %u\n",
	      p_measurement->port, p_measurement->voltage,
	      p_measurement->current, p_measurement->temperature,
	      p_measurement->power);
#endif
	ret = 0;
out:
	return ret;
}

int rtl8239_port_enable_set(struct poemgr_pse_chip *pse_chip, int port,
			    int enable_disable)
{
	int ret = -1;
	struct request port_enable_set = {
		.cmd = 0x01,
		.seq = -1,
		.chksm = -1,
	};

	memset(port_enable_set.params, 0xff, MAX_PARAMS);

	port_enable_set.params[0] = port;

	// Do not support semi-auto mode, only support auto and disable
	port_enable_set.params[1] = !!enable_disable;

	ret = rtl8239_request_response(pse_chip, &port_enable_set);
	if (ret) {
		perror("rtl8239_request_response(port_enable_Set): ");
		goto out;
	}

	ret = 0;
out:
	return ret;
}

int rtl8239_port_mibs_get(struct poemgr_pse_chip *pse_chip,
			  struct rtl8239_port_status *p_status)
{
	int ret = -1;

	struct request port_mibs_get = {
		.cmd = 0x45,
		.seq = -1,
		.params = { 0 },
		.chksm = -1,
	};

	if (!p_status) {
		ret = -ENOMEM;
		goto out;
	}

	port_mibs_get.params[0] = p_status->port;
	// do not reset mibs after the call
	port_mibs_get.params[1] = 0;

	ret = rtl8239_request_response(pse_chip, &port_mibs_get);
	if (ret) {
		perror("rtl8239_request_response(port_mibs_get): ");
		ret = -1;
		goto out;
	}

	if (p_status->port != port_mibs_get.response[2]) {
		debug("port and response does not match, expected:%02x != got:%02x\n",
		      p_status->port, port_mibs_get.response[2]);
		goto out;
	}

	p_status->mps_absent_counter = port_mibs_get.response[3];
	p_status->overload_counter = port_mibs_get.response[4];
	p_status->short_counter = port_mibs_get.response[5];
	p_status->power_denied_counter = port_mibs_get.response[6];
	p_status->invalid_signature_counter = port_mibs_get.response[7];

	ret = 0;

out:
	return ret;
}

int rtl8239_port_extended_config_get(struct poemgr_pse_chip *pse_chip,
				     struct rtl8239_port_status *p_status)
{
	int ret = -1;
	struct request port_basic_config_get = {
		.cmd = 0x48,
		.seq = -1,
		.params = { 0 },
		.chksm = -1,
	};

	struct request port_extended_config_get = {
		.cmd = 0x49,
		.seq = -1,
		.params = { 0 },
		.chksm = -1,
	};

	if (!p_status) {
		ret = -EINVAL;
		goto out;
	}

	port_basic_config_get.params[0] = p_status->port;

	ret = rtl8239_request_response(pse_chip, &port_basic_config_get);
	if (ret) {
		perror("rtl8239_request_response(port_basic_config_get): ");
		goto out;
	}

	if (port_basic_config_get.response[2] != p_status->port) {
		debug("port and response does not match, expected:%02x != got:%02x\n",
		      p_status->port, port_basic_config_get.response[2]);
		goto out;
	}

	port_extended_config_get.params[0] = p_status->port;
	ret = rtl8239_request_response(pse_chip, &port_extended_config_get);
	if (ret) {
		perror("rtl8239_request_response(port_extended_config_get)");
		goto out;
	}
	if (port_extended_config_get.response[2] != p_status->port) {
		debug("port and response does not match, expected:%02x != got:%02x\n",
		      p_status->port, port_extended_config_get.response[2]);
	}

	p_status->is_enabled = port_basic_config_get.response[3];
	p_status->function_mode = port_basic_config_get.response[4];
	p_status->det_type = port_basic_config_get.response[5];
	p_status->disconnect_type = port_basic_config_get.response[7];
	p_status->pair_type = port_basic_config_get.response[8];
	p_status->cable_type = port_basic_config_get.response[10];

	p_status->inrush_mode = port_extended_config_get.response[3];
	p_status->limit_type = port_extended_config_get.response[4];
	p_status->max_power = port_extended_config_get.response[5] *
			      400; // 0.4W/LSB = 400mw/LSB
	p_status->priority = port_extended_config_get.response[6];
	p_status->chipaddr = port_extended_config_get.response[7];
	p_status->primary_channel = port_extended_config_get.response[8];
	p_status->secondary_channel = port_extended_config_get.response[9];

	ret = 0;
out:
	return ret;
}

int rtl8239_export_port_metric(struct poemgr_pse_chip *pse_chip, int port,
			       struct poemgr_metric *output, int metric)
{
	int ret = -1;

	static struct rtl8239_port_status port_status = {
		.port = -1,
	};

	if (metric < 0)
		return -1;

	if (port_status.port != port) {
		// new port, repopulate everything..
		port_status.port = port;
		ret = rtl8239_port_status_get(pse_chip, &port_status);
		if (ret < 0) {
			perror("rtl8239_port_status_get: ");
			goto out;
		}

		ret = rtl8239_port_mibs_get(pse_chip, &port_status);
		if (ret < 0) {
			perror("rtl8239_port_mibs_get: ");
			goto out;
		}

		ret = rtl8239_port_extended_config_get(pse_chip, &port_status);
		if (ret < 0) {
			perror("rtl8239_port_basic_config_get: ");
			goto out;
		}
	}

	if (metric == 0) {
		output->type = POEMGR_METRIC_UINT32;
		output->name = "MPS Absent Counter";
		output->val_uint32 = port_status.mps_absent_counter;
	} else if (metric == 1) {
		output->type = POEMGR_METRIC_UINT32;
		output->name = "Overload Counter";
		output->val_uint32 = port_status.overload_counter;
	} else if (metric == 2) {
		output->type = POEMGR_METRIC_UINT32;
		output->name = "Short Counter";
		output->val_uint32 = port_status.short_counter;
	} else if (metric == 3) {
		output->type = POEMGR_METRIC_UINT32;
		output->name = "Power Denied Counter";
		output->val_uint32 = port_status.power_denied_counter;
	} else if (metric == 4) {
		output->type = POEMGR_METRIC_UINT32;
		output->name = "Invalid Signature Counter";
		output->val_uint32 = port_status.invalid_signature_counter;
// Following metrics are only for debugging
#ifdef ENABLE_DEBUG
	} else if (metric == 5) {
		output->type = POEMGR_METRIC_STRING;
		output->name = "Function Mode";
		switch (port_status.function_mode) {
		case 0:
			snprintf(output->val_char, sizeof(output->val_char),
				 "auto");
			break;
		case 1:
			snprintf(output->val_char, sizeof(output->val_char),
				 "semiauto");
			break;
		case 2:
			snprintf(output->val_char, sizeof(output->val_char),
				 "manual");
			break;
		}
	} else if (metric == 6) {
		output->type = POEMGR_METRIC_INT32;
		output->name = "Detection Type";
		output->val_int32 = port_status.det_type;
	} else if (metric == 7) {
		output->type = POEMGR_METRIC_STRING;
		output->name = "Disconnect Type";
		switch (port_status.disconnect_type) {
		case 0:
			snprintf(output->val_char, sizeof(output->val_char),
				 "Disable MPS");
			break;
		case 2:
			snprintf(output->val_char, sizeof(output->val_char),
				 "Enable MPS");
			break;
		case 3:
			snprintf(output->val_char, sizeof(output->val_char),
				 "Enable MPS after 700ms");
			break;
		case 1:
		default:
			snprintf(output->val_char, sizeof(output->val_char),
				 "!!Unknown!!");
			break;
		}
	} else if (metric == 8) {
		output->type = POEMGR_METRIC_STRING;
		output->name = "Pair Type";
		switch (port_status.pair_type) {
		case 0:
			snprintf(output->val_char, sizeof(output->val_char),
				 "Alternative A");
			break;
		case 1:
			snprintf(output->val_char, sizeof(output->val_char),
				 "Alternative B");
			break;
		default:
			snprintf(output->val_char, sizeof(output->val_char),
				 "!!Unknown!!");
			break;
		}
	} else if (metric == 9) {
		output->type = POEMGR_METRIC_STRING;
		output->name = "Cable Type";
		switch (port_status.cable_type) {
		case 0:
			snprintf(output->val_char, sizeof(output->val_char),
				 "Normal");
			break;
		case 1:
			snprintf(output->val_char, sizeof(output->val_char),
				 "Short");
			break;
		default:
			snprintf(output->val_char, sizeof(output->val_char),
				 "!!Unknown!!");
			break;
		}
	} else if (metric == 10) {
		output->type = POEMGR_METRIC_STRING;
		output->name = "Inrush Mode";
		switch (port_status.inrush_mode) {
		case 0:
			snprintf(output->val_char, sizeof(output->val_char),
				 "IEEE 802.3af");
			break;
		case 1:
			snprintf(output->val_char, sizeof(output->val_char),
				 "IEEE 802.3af High Inrush");
			break;
		case 2:
			snprintf(output->val_char, sizeof(output->val_char),
				 "IEEE 802.3at compatible");
			break;
		case 3:
			snprintf(output->val_char, sizeof(output->val_char),
				 "IEEE 802.3at");
			break;
		case 4:
			snprintf(output->val_char, sizeof(output->val_char),
				 "Pre-IEEE 802.3bt type 3");
			break;
		case 5:
			snprintf(output->val_char, sizeof(output->val_char),
				 "IEEE 802.3bt type 3");
			break;
		case 6:
			snprintf(output->val_char, sizeof(output->val_char),
				 "IEEE 802.3bt type 4");
			break;
		case 7:
			snprintf(output->val_char, sizeof(output->val_char),
				 "Pre-IEEE 802.3bt type 4");
			break;
		case 9:
			snprintf(output->val_char, sizeof(output->val_char),
				 "IEEE 802.3at ALT B");
			break;
		default:
			snprintf(output->val_char, sizeof(output->val_char),
				 "!!Unknown!!");
			break;
		}
	} else if (metric == 11) {
		output->type = POEMGR_METRIC_STRING;
		output->name = "Limit Type";
		switch (port_status.limit_type) {
		case 0:
			snprintf(output->val_char, sizeof(output->val_char),
				 "Class Based");
			break;
		case 1:
			snprintf(output->val_char, sizeof(output->val_char),
				 "User Defined");
			break;
		default:
			snprintf(output->val_char, sizeof(output->val_char),
				 "!!Unknown!!");
			break;
		}
	} else if (metric == 12) {
		output->type = POEMGR_METRIC_UINT32;
		output->name = "Allocated Power";
		output->val_uint32 = port_status.max_power;
	} else if (metric == 13) {
		output->type = POEMGR_METRIC_INT32;
		output->name = "Port Priority";
		output->val_int32 = port_status.priority;
	} else if (metric == 14) {
		output->type = POEMGR_METRIC_INT32;
		output->name = "Chip Address";
		output->val_int32 = port_status.chipaddr;
	} else if (metric == 15) {
		output->type = POEMGR_METRIC_INT32;
		output->name = "Primary Channel";
		output->val_int32 = port_status.primary_channel;
	} else if (metric == 16) {
		output->type = POEMGR_METRIC_INT32;
		output->name = "Secondary Channel";
		output->val_int32 = port_status.secondary_channel;
#endif
	} else {
		output->type = POEMGR_METRIC_END;
	}

	ret = 0;
out:
	return ret;
}

static int rtl8239_export_metric(struct poemgr_pse_chip *pse_chip,
				 struct poemgr_metric *output, int metric)
{
	int ret;

	static struct rtl8239_system_status system_status = { 0 };
	static struct rtl8239_system_power_status power_status = { 0 };

	if (metric < 0 || metric >= pse_chip->num_metrics)
		return -1;

	/* Only call once per process, since this does not change often.*/
	if (!system_status.max_ports) {
		ret = rtl8239_global_status_get(pse_chip, &system_status);
		if (ret) {
			perror("rtl8239_global_status_get(): ");
			goto out;
		}

		ret = rtl8239_global_power_status_get(pse_chip, &power_status);
		if (ret) {
			perror("rtl8239_global_power_status_get(): ");
			goto out;
		}

		ret = rtl8239_global_power_management_conf_get(pse_chip,
							       &power_status);
		if (ret) {
			perror("rtl8239_global_power_management_conf_get(): ");
			goto out;
		}

		rtl8239_port_event_status_get(pse_chip);
	}

	if (metric == 0) {
		output->type = POEMGR_METRIC_UINT32;
		output->name = "Allocated Power";
		output->val_uint32 = power_status.allocated_power;
	} else if (metric == 1) {
		output->type = POEMGR_METRIC_UINT32;
		output->name = "Available Power";
		output->val_uint32 = power_status.available_power;
	} else if (metric == 2) {
		output->type = POEMGR_METRIC_UINT32;
		output->name = "Current Power";
		output->val_uint32 = power_status.current_power;
	} else if (metric == 3) {
		output->type = POEMGR_METRIC_STRING;
		output->name = "Device ID";
		snprintf(output->val_char, sizeof(output->val_char), "%s",
			 rtl8239_device_id_str(system_status.device_id));
	} else if (metric == 4) {
		output->type = POEMGR_METRIC_STRING;
		output->name = "MCU Type";
		snprintf(output->val_char, sizeof(output->val_char), "%s",
			 system_status.mcu_type_str);
	} else if (metric == 5) {
		output->type = POEMGR_METRIC_INT32;
		output->name = "System Reset happened";
		output->val_int32 = system_status.system_reset_happened;
	} else if (metric == 6) {
		output->type = POEMGR_METRIC_INT32;
		output->name = "Bank Id";
		output->val_int32 = power_status.bank_id;
	} else if (metric == 7) {
		output->type = POEMGR_METRIC_STRING;
		output->name = "Bank Power Management Mode";
		switch (power_status.bank_mgmt_mode) {
		case NONE:
			snprintf(output->val_char, sizeof(output->val_char),
				 "None");
			break;
		case STATIC_WITH_PRIO:
			snprintf(output->val_char, sizeof(output->val_char),
				 "STATIC_WITH_PRIO");
			break;
		case DYNAMIC_WITH_PRIO:
			snprintf(output->val_char, sizeof(output->val_char),
				 "DYNAMIC_WITH_PRIO");
			break;
		case STATIC_NO_PRIO:
			snprintf(output->val_char, sizeof(output->val_char),
				 "STATIC_NO_PRIO");
			break;
		case DYNAMIC_NO_PRIO:
			snprintf(output->val_char, sizeof(output->val_char),
				 "DYNAMIC_NO_PRIO");
			break;
		default:
			snprintf(output->val_char, sizeof(output->val_char),
				 "!!Unknown!!");
			break;
		}
	} else if (metric == 8) {
		output->type = POEMGR_METRIC_INT32;
		output->name = "Bank Disable Preallocated Power";
		output->val_int32 = power_status.bank_disable_prealloc;
	} else if (metric == 9) {
		output->type = POEMGR_METRIC_UINT32;
		output->name = "Bank Total Power";
		output->val_uint32 = power_status.bank_total_power;
	} else if (metric == 10) {
		output->type = POEMGR_METRIC_UINT32;
		output->name = "Bank Reserved Power";
		output->val_uint32 = power_status.bank_reserved_power;
	} else {
		output->type = POEMGR_METRIC_END;
	}

out:
	return 0;
}

int rtl8239_init(struct poemgr_pse_chip *pse_chip, int i2c_bus, int i2c_addr,
		 uint32_t port_mask)
{
	struct rtl8239_priv *priv;
	char i2cpath[30];
	int fd;

	priv = malloc(sizeof(struct rtl8239_priv));
	if (!priv)
		return 1;

	priv->i2c_addr = i2c_addr;

	snprintf(i2cpath, sizeof(i2cpath) - 1, "/dev/i2c-%d", i2c_bus);
	fd = open(i2cpath, O_RDWR);

	if (fd == -1) {
		perror(i2cpath);
		goto out_free_priv;
	}

	if (ioctl(fd, I2C_SLAVE, priv->i2c_addr) < 0) {
		perror("i2c_set_address");
		goto out_free_fd;
	}

	priv->i2c_fd = fd;

	pse_chip->priv = (void *)priv;
	pse_chip->portmask = port_mask;
	pse_chip->model = "RTL8239";
	pse_chip->num_metrics = 11;
	pse_chip->export_metric = &rtl8239_export_metric;

	return 0;

out_free_fd:
	close(fd);
out_free_priv:
	free(priv);

	return 1;
}
