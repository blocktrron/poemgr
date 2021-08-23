#pragma once

#include <time.h>

#define POEMGR_MAX_PORTS	4

#define POEMGR_ACTION_STRING_SHOW	"show"
#define POEMGR_ACTION_STRING_APPLY	"apply"

enum {
	POEMGR_POE_TYPE_AF = 0x1,
	POEMGR_POE_TYPE_AT = 0x2,
	POEMGR_POE_TYPE_BT = 0x4,
};

static inline const char *poemgr_poe_type_to_string(int poe_type)
{
	if (poe_type == POEMGR_POE_TYPE_AF)
		return "802.3af";
	if (poe_type == POEMGR_POE_TYPE_AT)
		return "802.3at";
	if (poe_type == POEMGR_POE_TYPE_BT)
		return "802.3bt";

	return "unknown";
}

struct poemgr_ctx;

struct poemgr_profile {
	char *name;
	int num_ports;

	void *priv;

	int (*init)(struct poemgr_ctx *);
	int (*update_port_status)(struct poemgr_ctx *);
	int (*update_input_status)(struct poemgr_ctx *);
	int (*update_output_status)(struct poemgr_ctx *);
};

struct poemgr_port_settings {
	char *name;
	int disabled;
	int pse_port;
};

struct poemgr_port_status {
	int enabled;
	int active;
	int power_limit;
	int power;

	time_t last_update;
};

struct poemgr_port {
	struct poemgr_port_settings settings;
	struct poemgr_port_status status;
};

struct poemgr_input_status {
	int type;

	time_t last_update;
};

struct poemgr_output_status {
	int power_budget;

	time_t last_update;
};

struct poemgr_settings {
	int enabled;
	char *profile;
};

struct poemgr_ctx {
	struct poemgr_settings settings;
	struct poemgr_port ports[POEMGR_MAX_PORTS];
	struct poemgr_profile *profile;

	struct poemgr_input_status input_status;
	struct poemgr_output_status output_status;
};
