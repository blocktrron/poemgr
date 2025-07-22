/* SPDX-License-Identifier: GPL-2.0-only */

#pragma once

#include <time.h>
#include <stdint.h>

#define POEMGR_MAX_PORTS	8
#define POEMGR_MAX_PSE_CHIPS	2

#define POEMGR_MAX_METRICS		10

#define POEMGR_ACTION_STRING_ENABLE		"enable"
#define POEMGR_ACTION_STRING_DISABLE	"disable"
#define POEMGR_ACTION_STRING_SHOW		"show"
#define POEMGR_ACTION_STRING_APPLY		"apply"

enum poemgr_poe_type {
	POEMGR_POE_TYPE_AF = 0x1,
	POEMGR_POE_TYPE_AT = 0x2,
	POEMGR_POE_TYPE_BT = 0x4,
};

enum poemgr_port_fault_type {
	POEMGR_FAULT_TYPE_POWER_MANAGEMENT = 0x1,
	POEMGR_FAULT_TYPE_OVER_TEMPERATURE = 0x2,
	POEMGR_FAULT_TYPE_SHORT_CIRCUIT = 0x4,
	POEMGR_FAULT_TYPE_RESISTANCE_TOO_LOW = 0x8,
	POEMGR_FAULT_TYPE_RESISTANCE_TOO_HIGH = 0x10,
	POEMGR_FAULT_TYPE_CAPACITY_TOO_HIGH = 0x20,
	POEMGR_FAULT_TYPE_OPEN_CIRCUIT = 0x40,
	POEMGR_FAULT_TYPE_OVER_CURRENT = 0x80,
	POEMGR_FAULT_TYPE_UNKNOWN = 0x100,
};

enum poemgr_metric_type {
	POEMGR_METRIC_INT32,
	POEMGR_METRIC_UINT32,
	POEMGR_METRIC_STRING,

	POEMGR_METRIC_END,
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
	int poe_class;

	int faults;

	time_t last_update;
};

struct poemgr_port {
	struct poemgr_port_settings settings;
	struct poemgr_port_status status;
};

struct poemgr_input_status {
	enum poemgr_poe_type type;

	time_t last_update;
};

struct poemgr_output_status {
	int power_budget;
	enum poemgr_poe_type type;

	time_t last_update;
};

struct poemgr_settings {
	int disabled;
	int power_budget;
	char *profile;
};

struct poemgr_ctx {
	struct poemgr_settings settings;
	struct poemgr_port ports[POEMGR_MAX_PORTS];
	struct poemgr_profile *profile;

	struct poemgr_input_status input_status;
	struct poemgr_output_status output_status;
};

struct poemgr_metric {
	enum poemgr_metric_type type;
	char *name;
	union {
		char val_char[256];
		int32_t val_int32;
		uint32_t val_uint32;
	};
};

struct poemgr_pse_chip {
	const char *model;

	uint32_t portmask;

	void *priv;

	/* Metrics */
	int num_metrics;
	int (*export_metric)(struct poemgr_pse_chip *pse_chip, struct poemgr_metric *output, int metric);
};

struct poemgr_profile {
	char *name;
	int num_ports;

	struct poemgr_pse_chip pse_chips[POEMGR_MAX_PSE_CHIPS];
	int num_pse_chips;

	void *priv;

	int (*init)(struct poemgr_ctx *);
	int (*ready)(struct poemgr_ctx *);
	int (*enable)(struct poemgr_ctx *);
	int (*disable)(struct poemgr_ctx *);
	int (*apply_config)(struct poemgr_ctx *);
	int (*update_port_status)(struct poemgr_ctx *, int port);
	int (*update_input_status)(struct poemgr_ctx *);
	int (*update_output_status)(struct poemgr_ctx *);
	int (*export_port_metric)(struct poemgr_ctx *, int port, struct poemgr_metric *output, int metric);
};

static inline const char *poemgr_poe_type_to_string(enum poemgr_poe_type poe_type)
{
	if (poe_type == POEMGR_POE_TYPE_AF)
		return "802.3af";
	if (poe_type == POEMGR_POE_TYPE_AT)
		return "802.3at";
	if (poe_type == POEMGR_POE_TYPE_BT)
		return "802.3bt";

	return "unknown";
}

static inline struct poemgr_pse_chip *poemgr_profile_pse_chip_get(struct poemgr_profile *profile, int pse_idx)
{
	return &profile->pse_chips[pse_idx];
} 
