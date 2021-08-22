#include "poemgr.h"
#include "pd69104.h"

static struct pd69104_priv psechip;

struct poemgr_profile poemgr_profile_uswflex = {
	.name = "usw-flex",
	.init = &poemgr_uswflex_init_chip,
	.priv = &psechip,
};

static int poemgr_uswflex_init_chip(struct poemgr_ctx *ctx) {
	/* ToDo: Toggle shift register */

	/* Init PD69104 */
	if (pd69104_init(&psechip, 0, 0x20))
		return 1;

	return 0;
}
