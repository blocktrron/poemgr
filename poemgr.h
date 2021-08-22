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

struct poemgr_ctx;

struct poemgr_profile {
	char *name;
	int num_ports;

	void *priv;

	int (*init)(struct poemgr_ctx *);
	int (*update_port_status)(struct poemgr_ctx *);
	int (*update_pse_status)(struct poemgr_ctx *);
};

struct poemgr_port_settings {
	char *name;
	int disabled;
	int pse_port;
};

struct poemgr_port_status {
	int enabled;
	int power_limit;
	int power;

	time_t last_update;
};

struct poemgr_port {
	struct poemgr_port_settings settings;
	struct poemgr_port_status status;
};

struct poemgr_settings {
	int enabled;
	char *profile;
};

struct poemgr_ctx {
	struct poemgr_settings settings;
	struct poemgr_port ports[POEMGR_MAX_PORTS];
	struct poemgr_profile *profile;
};
