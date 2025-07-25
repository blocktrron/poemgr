/* SPDX-License-Identifier: GPL-2.0-only */

#include <errno.h>
#include <stdio.h>

#include "common.h"
#include "poemgr.h"
#include "rtl8239.h"

#define PSX28_NUM_PORTS 24
#define PSX28_NUM_PSE_CHIPS 1
#define PSX28_NUM_PSE_CHIP_IDX 0
#define PSX28_PSE_PORTMASK 0xff
#define PSX28_PSE_I2C_ADDR 0x20

#define PSX28_OWN_POWER_BUDGET 400U

static int poemgr_psx28_init_chip(struct poemgr_ctx *ctx)
{
	struct poemgr_pse_chip *psechip = poemgr_profile_pse_chip_get(
		ctx->profile, PSX28_NUM_PSE_CHIP_IDX);

	return rtl8239_init(psechip, 5, PSX28_PSE_I2C_ADDR, PSX28_PSE_PORTMASK);
}

static int poemgr_psx28_ready(struct poemgr_ctx *ctx)
{
	int ret = -1;
	struct poemgr_pse_chip *psechip = poemgr_profile_pse_chip_get(
		ctx->profile, PSX28_NUM_PSE_CHIP_IDX);

	ret = rtl8239_device_online(psechip);
	if (ret != 0) {
		perror("rtl8239_device_online");
		ret = 0;
		goto out;
	}
	ret = 1;
out:
	return ret;
}

static int poemgr_psx28_enable_chip(struct poemgr_ctx *ctx)
{
	int ret = -1;
	struct poemgr_pse_chip *psechip = poemgr_profile_pse_chip_get(
		ctx->profile, PSX28_NUM_PSE_CHIP_IDX);

	struct rtl8239_system_power_status conf = {
		.bank_mgmt_mode = DYNAMIC_NO_PRIO,
		.bank_disable_prealloc = 0,
	};

	ret = rtl8239_global_enable(psechip, 1);
	if (ret) {
		perror("rtl8239_global_enable");
		ret = -EINVAL;
		goto out;
	}

	ret = rtl8239_global_power_management_mode_set(psechip, conf);
	if (ret) {
		perror("rtl8239_global_power_management_mode_set");
		ret = -EINVAL;
		goto out;
	}

	ret = 0;
out:
	return ret;
}

static int poemgr_psx28_disable_chip(struct poemgr_ctx *ctx)
{
	int ret = -1;
	struct poemgr_pse_chip *psechip = poemgr_profile_pse_chip_get(
		ctx->profile, PSX28_NUM_PSE_CHIP_IDX);

	ret = rtl8239_global_enable(psechip, 0);
	if (ret) {
		perror("rtl8239_global_enable");
		ret = -EINVAL;
		goto out;
	}

	ret = rtl8239_device_reset(psechip);
	if (ret != 0) {
		perror("rtl8239_device_reset, ignoreing");
		ret = 0;
	}

	ret = 0;
out:
	return ret;
}

static int poemgr_psx28_update_port_status(struct poemgr_ctx *ctx, int port)
{
	int ret = -1;
	struct poemgr_pse_chip *psechip = poemgr_profile_pse_chip_get(
		ctx->profile, PSX28_NUM_PSE_CHIP_IDX);
	struct poemgr_port_status *p_poemgr_port_status =
		&ctx->ports[port].status;
	struct rtl8239_port_status port_status = { 0 };
	struct rtl8239_port_measurement port_measurement = { 0 };

	port_status.port = port;
	port_measurement.port = port;

	ret = rtl8239_port_status_get(psechip, &port_status);
	if (ret < 0) {
		perror("rtl8239_port_status_get: ");
		goto out;
	}

	ret = rtl8239_port_measurement_get(psechip, &port_measurement);
	if (ret < 0) {
		perror("rtl8239_port_measurement_get: ");
		goto out;
	}

	// reset faults
	p_poemgr_port_status->faults = 0;

	p_poemgr_port_status->enabled =
		!(port_status.power_status == DISABLED ||
		  port_status.power_status == POWER_STATUS_ERROR);
	p_poemgr_port_status->active =
		(port_status.power_status == DELIVERING_POWER);
	// p_poemgr_port_status->power_limit =
	p_poemgr_port_status->power = port_measurement.power;

	if (port_status.classification_result == PD0 ||
	    port_status.classification_result == PD_TREATED_AS_CLASS0)
		p_poemgr_port_status->poe_class = 0;
	else if (port_status.classification_result >= PD1 &&
		 port_status.classification_result <= PD8)
		p_poemgr_port_status->poe_class =
			port_status.classification_result;
	else if (port_status.classification_result == CLASS_MISMATCH)
		p_poemgr_port_status->poe_class = -1;
	else if (port_status.classification_result == CLASS_OVERCURRENT) {
		p_poemgr_port_status->poe_class = -2;
		p_poemgr_port_status->faults |= POEMGR_FAULT_TYPE_OVER_CURRENT;
	} else
		p_poemgr_port_status->poe_class = -3;

	if (port_status.power_status == FAULT ||
	    port_status.power_status == POWER_STATUS_ERROR) {
		switch (port_status.error_type) {
		case OVLO:
		case UVLO:
		case POWER_DENIED:
		case INRUSH_FAIL:
			p_poemgr_port_status->faults |=
				POEMGR_FAULT_TYPE_POWER_MANAGEMENT;
			break;
		case SHORT:
			p_poemgr_port_status->faults |=
				POEMGR_FAULT_TYPE_SHORT_CIRCUIT;
			break;
		case THERMAL_SHUTDOWN:
			p_poemgr_port_status->faults |=
				POEMGR_FAULT_TYPE_OVER_TEMPERATURE;
			break;
		case OVERLOAD:
			p_poemgr_port_status->faults |=
				POEMGR_FAULT_TYPE_OVER_CURRENT;
			break;
		default:
			break;
		}

		switch (port_status.detection_result) {
		case SHORT_CIRCUIT:
			p_poemgr_port_status->faults |=
				POEMGR_FAULT_TYPE_SHORT_CIRCUIT;
			break;
		case HIGH_CAP:
			p_poemgr_port_status->faults |=
				POEMGR_FAULT_TYPE_CAPACITY_TOO_HIGH;
			break;
		case RLOW:
			p_poemgr_port_status->faults |=
				POEMGR_FAULT_TYPE_RESISTANCE_TOO_LOW;
			break;
		case RHIGH:
			p_poemgr_port_status->faults |=
				POEMGR_FAULT_TYPE_RESISTANCE_TOO_HIGH;
			break;
		case OPEN_CIRCUIT:
			p_poemgr_port_status->faults |=
				POEMGR_FAULT_TYPE_OPEN_CIRCUIT;
			break;
		case DETECTION_ERROR:
			p_poemgr_port_status->faults |=
				POEMGR_FAULT_TYPE_UNKNOWN;
			break;
		default:
			break;
		}
	}

	ret = 0;

out:
	return ret;
}

static int poemgr_psx28_apply_config(struct poemgr_ctx *ctx)
{
	struct poemgr_pse_chip *psechip = poemgr_profile_pse_chip_get(
		ctx->profile, PSX28_NUM_PSE_CHIP_IDX);
	struct poemgr_port_settings *port_settings;
	int port_settings_available;
	int port_enable_disable;
	int power_budget;
	int ret = 0;

	power_budget = ctx->settings.power_budget ? ctx->settings.power_budget :
						    PSX28_OWN_POWER_BUDGET;

	ret = rtl8239_global_power_source_set(psechip, power_budget);
	if (ret < 0)
		goto out;

	for (int i = 0; i < PSX28_NUM_PORTS; i++) {
		port_settings = &ctx->ports[i].settings;
		port_settings_available = !!port_settings->name;

		port_enable_disable = 1;
		if (port_settings->disabled || !port_settings_available)
			port_enable_disable = 0;

		ret = rtl8239_port_max_power_type_set(psechip, i, CLASS_BASED);
		if (ret) {
			debug("rtl8239_port_max_power_type_set(i:%d, CLASS_BASED) Failed\n",
			      i);
			perror("rtl8239_port_max_power_type_set(): ");
			goto out;
		}

		ret = rtl8239_port_enable_set(psechip, i, port_enable_disable);
		if (ret) {
			debug("rtl8239_port_enable_set(i:%d, enable_disable:%d): Failed\n",
			      i, port_enable_disable);
			perror("rtl8239_port_enable_set(): ");
			goto out;
		}
	}

	ret = 0;
out:
	return ret;
}

static int poemgr_psx28_export_port_metric(struct poemgr_ctx *ctx, int port,
					   struct poemgr_metric *output,
					   int metric)
{
	struct poemgr_pse_chip *psechip = poemgr_profile_pse_chip_get(
		ctx->profile, PSX28_NUM_PSE_CHIP_IDX);
	return rtl8239_export_port_metric(psechip, port, output, metric);
}

static int poemgr_psx28_update_output_status(struct poemgr_ctx *ctx)
{
	int power_budget;

	power_budget = ctx->settings.power_budget ? ctx->settings.power_budget :
						    PSX28_OWN_POWER_BUDGET;
	ctx->output_status.power_budget = power_budget * 1000;
	ctx->output_status.type = POEMGR_POE_TYPE_BT;
	return 0;
}

static int poemgr_psx28_update_input_status(struct poemgr_ctx *ctx)
{
	return 0;
}

struct poemgr_profile poemgr_profile_psx28 = {
	.name = "psx28",
	.num_ports = PSX28_NUM_PORTS,
	.ready = &poemgr_psx28_ready,
	.enable = &poemgr_psx28_enable_chip,
	.disable = &poemgr_psx28_disable_chip,
	.init = &poemgr_psx28_init_chip,
	.apply_config = &poemgr_psx28_apply_config,
	.update_port_status = &poemgr_psx28_update_port_status,
	.export_port_metric = &poemgr_psx28_export_port_metric,
	.update_output_status = &poemgr_psx28_update_output_status,
	.update_input_status = &poemgr_psx28_update_input_status,
	.num_pse_chips = PSX28_NUM_PSE_CHIPS,
};
