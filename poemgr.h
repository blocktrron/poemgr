#pragma once

#define POEMGR_MAX_PORTS	10

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
};

struct poemgr_port_settings {
	char *name;
	int pse_port;
	int priority;
	int max_output;
};

struct poemgr_port {
	struct poemgr_port_settings settings;
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
