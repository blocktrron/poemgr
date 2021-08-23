#include <stdlib.h>
#include <stdio.h>

#include "poemgr.h"
#include "pd69104.h"

#define USWLFEX_NUM_PORTS	4

#define USWLFEX_OWN_POWER_BUDGET	5	/* Own power budget */

static struct pd69104_priv psechip;

static int poemgr_uswflex_read_power_input(struct poemgr_ctx *ctx)
{
	int reg;

	/* PSE has 4 input pins (4 bits in register), the USW-Flex only cares for the first 3 LSB */
	reg = pd69104_pwrgd_pin_status_get(&psechip) & 0x7;
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

static int poemgr_uswflex_get_power_budget(int poe_type)
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
	int poe_type = poemgr_uswflex_read_power_input(ctx);

	/* Toggle FlipFlop */
	/* ToDo Replace this with libgpiod at some point. Not part of OpenWrt core yet. */
	system("/usr/lib/poemgr/uswlite-pse-enable &> /dev/null");

	/* Init PD69104 */
	if (pd69104_init(&psechip, 0, 0x20))
		return 1;

	return 0;
}

static int poemgr_uswflex_update_single_port_status(struct poemgr_ctx *ctx, int port)
{
	struct poemgr_port_status *port_status = &ctx->ports[port].status;

	port_status->power = pd69104_port_power_consumption_get(&psechip, port);
	port_status->power_limit = pd69104_port_power_limit_get(&psechip, port);
	port_status->enabled = !!port_status->power_limit;

	return 0;
}


static int poemgr_uswflex_update_port_status(struct poemgr_ctx *ctx)
{
	for (int i = 0; i < USWLFEX_NUM_PORTS; i++) {
		poemgr_uswflex_update_single_port_status(ctx, i);
	}
}

static int poemgr_uswflex_update_output_status(struct poemgr_ctx *ctx)
{
	ctx->output_status.power_budget = poemgr_uswflex_get_power_budget(poemgr_uswflex_read_power_input(ctx));

	return 0;
}

static int poemgr_uswflex_update_input_status(struct poemgr_ctx *ctx)
{
	ctx->input_status.type = poemgr_uswflex_read_power_input(ctx);

	return 0;
}

struct poemgr_profile poemgr_profile_uswflex = {
	.name = "usw-flex",
	.num_ports = USWLFEX_NUM_PORTS,
	.init = &poemgr_uswflex_init_chip,
	.update_port_status = &poemgr_uswflex_update_port_status,
	.update_output_status = &poemgr_uswflex_update_output_status,
	.update_input_status = &poemgr_uswflex_update_input_status,
	.priv = &psechip,
};
