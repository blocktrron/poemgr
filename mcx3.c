/* SPDX-License-Identifier: GPL-2.0-only */

#include <stdio.h>

#include "ip802ar.h"
#include "poemgr.h"

#define MCX3_NUM_PORTS 2
#define MCX3_NUM_PSE_CHIPS 1
#define MCX3_NUM_PSE_CHIP_IDX 0
#define MCX3_PSE_PORTMASK 0b11
#define MCX3_PSE_I2C_ADDR 0x74

#define MCX3_OWN_POWER_BUDGET (60U)
#define MCX3_POE_VOLTAGE (54U)

static int poemgr_mcx3_init_chip(struct poemgr_ctx *ctx)
{
	struct poemgr_pse_chip *psechip = poemgr_profile_pse_chip_get(
		ctx->profile, MCX3_NUM_PSE_CHIP_IDX);

	return ip802ar_init(psechip, 0, MCX3_PSE_I2C_ADDR, MCX3_PSE_PORTMASK);
}

static int poemgr_mcx3_ready(struct poemgr_ctx *ctx)
{
	int ret = -1;
	struct poemgr_pse_chip *psechip = poemgr_profile_pse_chip_get(
		ctx->profile, MCX3_NUM_PSE_CHIP_IDX);

	ret = ip802ar_device_online(psechip);
	if (ret != 0) {
		perror("ip802ar_device_online");
		ret = 0;
		goto out;
	}
	ret = 1;
out:
	return ret;
}

static int poemgr_mcx3_enable_chip(struct poemgr_ctx *ctx)
{
	int ret;
	struct poemgr_pse_chip *psechip = poemgr_profile_pse_chip_get(
		ctx->profile, MCX3_NUM_PSE_CHIP_IDX);

	ret = ip802ar_device_enable_set(psechip, 1);
	if (ret < 0)
		goto out;

	for (int i = 0; i < MCX3_NUM_PORTS; i++) {
		ret = ip802ar_port_enable_set(psechip, i, 1);
		if (ret < 0)
			goto out;
	}

out:
	return ret;
}

static int poemgr_mcx3_disable_chip(struct poemgr_ctx *ctx)
{
	int ret = 0;
	struct poemgr_pse_chip *psechip = poemgr_profile_pse_chip_get(
		ctx->profile, MCX3_NUM_PSE_CHIP_IDX);
	ret = ip802ar_device_enable_set(psechip, 0);
	for (int i = 0; i < MCX3_NUM_PORTS; i++) {
		ret = ip802ar_port_enable_set(psechip, i, 0);
		if (ret < 0)
			goto out;
	}
out:
	return ret;
}

static int poemgr_mcx3_update_port_status(struct poemgr_ctx *ctx, int port)
{
	struct poemgr_pse_chip *psechip = poemgr_profile_pse_chip_get(
		ctx->profile, MCX3_NUM_PSE_CHIP_IDX);
	struct poemgr_port_status *port_status = &ctx->ports[port].status;

	port_status->power = ip802ar_port_power_consumption_get(psechip, port);
	port_status->active = ip802ar_port_good_get(psechip, port);
	port_status->power_limit = ip802ar_port_power_limit_get(psechip, port);
	port_status->enabled = ip802ar_port_enable_get(psechip, port);
	port_status->faults = ip802ar_port_faults_get(psechip, port);
	port_status->poe_class = ip802ar_port_poe_class_get(psechip, port);

	return 0;
}

static int poemgr_mcx3_apply_config(struct poemgr_ctx *ctx)
{
	struct poemgr_pse_chip *psechip = poemgr_profile_pse_chip_get(
		ctx->profile, MCX3_NUM_PSE_CHIP_IDX);
	struct poemgr_port_settings *port_settings;
	int port_settings_available;
	int power_budget;
	int port_opmode;
	int ret = 0;

	power_budget = ctx->settings.power_budget ? ctx->settings.power_budget :
						    MCX3_OWN_POWER_BUDGET;

	ret = ip802ar_system_power_budget_set(psechip, 0, power_budget);
	if (ret < 0)
		goto out;

	ret = ip802ar_system_power_budget_set(psechip, 1, power_budget);
	if (ret < 0)
		goto out;

	ret = ip802ar_system_current_budget_set(
		psechip, power_budget * 1000 / MCX3_POE_VOLTAGE);
	if (ret < 0)
		goto out;

	for (int i = 0; i < MCX3_NUM_PORTS; i++) {
		port_settings = &ctx->ports[i].settings;
		port_settings_available = !!port_settings->name;

		port_opmode = 1;
		if (port_settings->disabled || !port_settings_available)
			port_opmode = 0;
		ret = ip802ar_port_enable_set(psechip, i, port_opmode);
		if (ret < 0)
			goto out;
	}
out:
	return ret;
}

static int poemgr_mcx3_update_output_status(struct poemgr_ctx *ctx)
{
	struct poemgr_pse_chip *psechip = poemgr_profile_pse_chip_get(
		ctx->profile, MCX3_NUM_PSE_CHIP_IDX);
	int power_budget;

	power_budget = ctx->settings.power_budget ? ctx->settings.power_budget :
						    MCX3_OWN_POWER_BUDGET;

	ctx->output_status.power_budget = power_budget * 1000;
	ctx->output_status.type = POEMGR_POE_TYPE_AT;

	ip802ar_clear_faults(psechip, MCX3_NUM_PORTS);
	return 0;
}

static int poemgr_mcx3_update_input_status(struct poemgr_ctx *ctx)
{
	return 0;
}

static int poemgr_mcx3_export_port_metric(struct poemgr_ctx *ctx, int port,
					  struct poemgr_metric *output,
					  int metric)
{
	struct poemgr_pse_chip *psechip = poemgr_profile_pse_chip_get(
		ctx->profile, MCX3_NUM_PSE_CHIP_IDX);
	return ip802ar_export_port_metric(
		psechip, port, &ctx->ports[port].status, output, metric);
}

struct poemgr_profile poemgr_profile_mcx3 = {
	.name = "mcx3",
	.num_ports = MCX3_NUM_PORTS,
	.ready = &poemgr_mcx3_ready,
	.enable = &poemgr_mcx3_enable_chip,
	.disable = &poemgr_mcx3_disable_chip,
	.init = &poemgr_mcx3_init_chip,
	.apply_config = &poemgr_mcx3_apply_config,
	.update_port_status = &poemgr_mcx3_update_port_status,
	.export_port_metric = &poemgr_mcx3_export_port_metric,
	.update_output_status = &poemgr_mcx3_update_output_status,
	.update_input_status = &poemgr_mcx3_update_input_status,
	.num_pse_chips = MCX3_NUM_PSE_CHIPS,
};
