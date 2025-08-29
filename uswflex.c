/* SPDX-License-Identifier: GPL-2.0-only */

#include <stdlib.h>
#include <stdio.h>

#include "poemgr.h"
#include "pd69104.h"
#include "pd69104_regs.h"

#define USWLFEX_NUM_PORTS	4
#define USWLFEX_NUM_PSE_CHIPS	1
#define USWLFEX_NUM_PSE_CHIP_IDX	0
#define USWFLEX_PSE_PORTMASK	0xF

#define USWLFEX_OWN_POWER_BUDGET	5	/* Own power budget */

static enum poemgr_poe_type poemgr_uswflex_read_power_input(struct poemgr_ctx *ctx)
{
	struct poemgr_pse_chip *psechip = poemgr_profile_pse_chip_get(ctx->profile, USWLFEX_NUM_PSE_CHIP_IDX);
	int reg;

	/* PSE has 4 input pins (4 bits in register), the USW-Flex only cares for the first 3 LSB */
	reg = pd69104_pwrgd_pin_status_get(psechip) & 0x7;
	if (reg < 0)
		return -1;

	switch(reg) {
		case 0:
		/* 1: Non-standard PoE++ */
		case 1:
		case 2:
		/* 3: Included adapter */
		case 3:
		case 4:
		case 6:
			return POEMGR_POE_TYPE_BT;
		case 5:
			return POEMGR_POE_TYPE_AT;
		case 7:
			return POEMGR_POE_TYPE_AF;
		default:
			fprintf(stderr, "Unknown PoE input 0x%02X\n", reg);
	}

	return -1;
}

static int poemgr_uswflex_get_power_budget(enum poemgr_poe_type poe_type)
{
	/* Watts */
	switch (poe_type) {
		case POEMGR_POE_TYPE_BT:
			return 51 - USWLFEX_OWN_POWER_BUDGET;
		case POEMGR_POE_TYPE_AT:
			return 25 - USWLFEX_OWN_POWER_BUDGET;
		case POEMGR_POE_TYPE_AF:
		default:
			return 13 - USWLFEX_OWN_POWER_BUDGET;
	}
}

static int poemgr_uswflex_init_chip(struct poemgr_ctx *ctx) {
	struct poemgr_pse_chip *psechip = poemgr_profile_pse_chip_get(ctx->profile, USWLFEX_NUM_PSE_CHIP_IDX);

	/* Init PD69104 */
	if (pd69104_init(psechip, 0, 0x20, USWFLEX_PSE_PORTMASK))
		return 1;

	return 0;
}

static int poemgr_uswflex_ready(struct poemgr_ctx *ctx) {
	struct poemgr_pse_chip *psechip = poemgr_profile_pse_chip_get(ctx->profile, USWLFEX_NUM_PSE_CHIP_IDX);

	/* Check if PSE is up. */
	return pd69104_device_online(psechip);
}

static int poemgr_uswflex_enable_chip(struct poemgr_ctx *ctx) {
	int pse_reachable;

	/* Check if PSE is up. Only reset the PSE chip in case the device is not reachable. */
	pse_reachable = poemgr_uswflex_ready(ctx);
	if (!pse_reachable) {
		/* Toggle FlipFlop */
		/* ToDo Replace this with libgpiod at some point. Not part of OpenWrt core yet. */
		system("/usr/lib/poemgr/uswlite-pse-enable 0 &> /dev/null");
	}

	return 0;
}

static int poemgr_uswflex_disable_chip(struct poemgr_ctx *ctx)
{
	/* Always disable chip, regardless whether it is reachable or not */
	system("/usr/lib/poemgr/uswlite-pse-enable 1 &> /dev/null");

	return 0;
}

static int poemgr_uswflex_update_port_status(struct poemgr_ctx *ctx, int port)
{
	struct poemgr_pse_chip *psechip = poemgr_profile_pse_chip_get(ctx->profile, USWLFEX_NUM_PSE_CHIP_IDX);
	struct poemgr_port_status *port_status = &ctx->ports[port].status;

	port_status->power = pd69104_port_power_consumption_get(psechip, port);
	port_status->active = pd69104_port_power_good_get(psechip, port);
	port_status->power_limit = pd69104_port_power_limit_get(psechip, port);
	port_status->enabled = pd69104_port_operation_mode_get(psechip, port) == PD69104_REG_OPMD_AUTO;
	port_status->faults = pd69104_port_faults_get(psechip, port);
	port_status->poe_class = pd69104_port_poe_class_get(psechip, port);

	return 0;
}

static int poemgr_uswflex_update_output_status(struct poemgr_ctx *ctx)
{
	int poe_budget;
	if (ctx->settings.power_budget > 0) {
		poe_budget = ctx->settings.power_budget;
	} else {
		poe_budget = poemgr_uswflex_get_power_budget(poemgr_uswflex_read_power_input(ctx));
	}

	ctx->output_status.power_budget = poe_budget;
	ctx->output_status.type = POEMGR_POE_TYPE_AT;

	return 0;
}

static int poemgr_uswflex_update_input_status(struct poemgr_ctx *ctx)
{
	ctx->input_status.type = poemgr_uswflex_read_power_input(ctx);

	return 0;
}

static int poemgr_uswflex_apply_config(struct poemgr_ctx *ctx)
{
	struct poemgr_pse_chip *psechip = poemgr_profile_pse_chip_get(ctx->profile, USWLFEX_NUM_PSE_CHIP_IDX);
	struct poemgr_port_settings *port_settings;
	int port_settings_available;
	int port_opmode;
	int ret = 0;

	int poe_budget;
	if (ctx->settings.power_budget > 0) {
		poe_budget = ctx->settings.power_budget;
	} else {
		poe_budget = poemgr_uswflex_get_power_budget(poemgr_uswflex_read_power_input(ctx));
	}

	/* Set global power limit (Input - CPU)
	 * Write this to all banks (a bank maps to the state of PGD[2:0]).
	 */
	for (int i = 0; i < PD69104_REG_PWR_BNK_NUM_BANKS; i++) {
		ret = pd69104_system_power_budget_set(psechip, i, poe_budget);
		if (ret < 0)
			goto out;
	}

	for (int i = 0; i < USWLFEX_NUM_PORTS; i++) {
		/* Apply settings */
		port_settings = &ctx->ports[i].settings;
		port_settings_available = !!port_settings->name;

		/* Set port operation mode */
		port_opmode = PD69104_REG_OPMD_AUTO;
		if (port_settings->disabled || !port_settings_available)
			port_opmode = PD69104_REG_OPMD_SHUTDOWN;
		ret = pd69104_port_operation_mode_set(psechip, i, port_opmode);
		if (ret < 0)
			goto out;

		/* Shutdown implicitly disables detection as well as classification */
		if (port_opmode != PD69104_REG_OPMD_SHUTDOWN) {
			ret = pd69104_port_detection_classification_set(psechip, i, 1);
			if (ret < 0)
				goto out;
		}

		/* Set output limit per port */
		ret = pd69104_port_power_limit_set(psechip, i, poe_budget);
		if (ret < 0)
			goto out;

	}

	/* ToDo: Set output priority */
out:
	return 0;
}

struct poemgr_profile poemgr_profile_uswflex = {
	.name = "usw-flex",
	.num_ports = USWLFEX_NUM_PORTS,
	.ready = &poemgr_uswflex_ready,
	.enable = &poemgr_uswflex_enable_chip,
	.disable = &poemgr_uswflex_disable_chip,
	.init = &poemgr_uswflex_init_chip,
	.apply_config = &poemgr_uswflex_apply_config,
	.update_port_status = &poemgr_uswflex_update_port_status,
	.update_output_status = &poemgr_uswflex_update_output_status,
	.update_input_status = &poemgr_uswflex_update_input_status,
	.num_pse_chips = USWLFEX_NUM_PSE_CHIPS,
};
