#include <stdlib.h>

#include "poemgr.h"
#include "pd69104.h"

#define USWLFEX_NUM_PORTS	4

static struct pd69104_priv psechip;

static int poemgr_uswflex_init_chip(struct poemgr_ctx *ctx) {
	/* Toggle FlipFlop */
	/* ToDo Replace this with libgpiod at some point. Not part of OpenWrt core yet. */
	system("/usr/lib/poemgr/uswlite-pse-enable");

	/* Init PD69104 */
	if (pd69104_init(&psechip, 0, 0x20))
		return 1;

	return 0;
}

static int poemgr_uswflex_update_single_port_status(struct poemgr_ctx *ctx, int port)
{
	struct poemgr_port_status *port_status = &ctx->ports[port].status;

	port_status->power = pd69104_port_power_consumption_get(&psechip, port);

	return 0;
}


static int poemgr_uswflex_update_port_status(struct poemgr_ctx *ctx)
{
	for (int i = 0; i < USWLFEX_NUM_PORTS; i++) {
		poemgr_uswflex_update_single_port_status(ctx, i);
	}
}

struct poemgr_profile poemgr_profile_uswflex = {
	.name = "usw-flex",
	.num_ports = USWLFEX_NUM_PORTS,
	.init = &poemgr_uswflex_init_chip,
	.update_port_status = &poemgr_uswflex_update_port_status,
	.priv = &psechip,
};
