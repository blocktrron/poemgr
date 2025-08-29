/* SPDX-License-Identifier: GPL-2.0-only */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uci.h>
#include <unistd.h>
#include <json.h>

#include "poemgr.h"

extern struct poemgr_profile poemgr_profile_psx10;
extern struct poemgr_profile poemgr_profile_uswflex;

static struct poemgr_profile *poemgr_profiles[] = {
	&poemgr_profile_psx10,
	&poemgr_profile_uswflex,
	NULL
};

static int uci_lookup_option_int(struct uci_context* uci, struct uci_section* s,
								 const char* name)
{
	const char* str = uci_lookup_option_string(uci, s, name);
	return str == NULL ? -1 : atoi(str);
}

static int poemgr_load_port_settings(struct poemgr_ctx *ctx, struct uci_context *uci_ctx)
{
	const char *disabled, *port, *name;
	struct uci_package *package;
	struct uci_element *e;
	struct uci_section *s;
	int port_idx;
	int ret = 0;

	package = uci_lookup_package(uci_ctx, "poemgr");

	if (!package) {
		ret = 1;
		goto out;
	}

	uci_foreach_element(&package->sections, e) {
		s = uci_to_section(e);

		if (strcmp(s->type, "port"))
			continue;

		port = uci_lookup_option_string(uci_ctx, s, "port");
		name = uci_lookup_option_string(uci_ctx, s, "name");
		disabled = uci_lookup_option_string(uci_ctx, s, "disabled");

		if (!port) {
			ret = 1;
			goto out;
		}

		port_idx = atoi(port);
		if (port_idx == -1) {
			/* No port specified */
			ret = 1;
			goto out;
		} else if (port_idx >= ctx->profile->num_ports) {
			/* Port does not exist. Ignore. */
			continue;
		}

		ctx->ports[port_idx].settings.name =  name ? strdup(name) : strdup(port);
		ctx->ports[port_idx].settings.disabled = disabled ? !!atoi(disabled) : 0;
	}
out:
	return ret;
}

int poemgr_load_settings(struct poemgr_ctx *ctx, struct uci_context *uci_ctx)
{
	struct uci_package *package;
	struct uci_section *section;
	const char *s;
	int ret;

	ret = 0;

	if (!uci_ctx)
		return -1;

	if (uci_load(uci_ctx, "poemgr", &package) != UCI_OK) {
		ret = -1;
		goto out;
	}

	section = uci_lookup_section(uci_ctx, package, "settings");
	if (!section || strcmp(section->type, "poemgr")) {
		ret = -1;
		goto out;
	}

	ctx->settings.disabled = !!(uci_lookup_option_int(uci_ctx, section, "disabled") > 0);
	ctx->settings.power_budget = uci_lookup_option_int(uci_ctx, section, "power_budget");

	s = uci_lookup_option_string(uci_ctx, section, "profile");
	if (!s) {
		ret = -1;
		goto out;
	}

	ctx->settings.profile = strdup(s);

out:
	return ret;
}

static json_object *poemgr_create_port_fault_array(int faults)
{
	struct json_object *arr = json_object_new_array();

	if (faults & POEMGR_FAULT_TYPE_POWER_MANAGEMENT)
		json_object_array_add(arr, json_object_new_string("power-budget-exceeded"));
	if (faults & POEMGR_FAULT_TYPE_OVER_TEMPERATURE)
		json_object_array_add(arr, json_object_new_string("over-temperature"));
	if (faults & POEMGR_FAULT_TYPE_SHORT_CIRCUIT)
		json_object_array_add(arr, json_object_new_string("short-circuit"));
	if (faults & POEMGR_FAULT_TYPE_RESISTANCE_TOO_LOW)
		json_object_array_add(arr, json_object_new_string("resistance-too-low"));
	if (faults & POEMGR_FAULT_TYPE_RESISTANCE_TOO_HIGH)
		json_object_array_add(arr, json_object_new_string("resistance-too-high"));
	if (faults & POEMGR_FAULT_TYPE_CAPACITY_TOO_HIGH)
		json_object_array_add(arr, json_object_new_string("capacity-too-high"));
	if (faults & POEMGR_FAULT_TYPE_OPEN_CIRCUIT)
		json_object_array_add(arr, json_object_new_string("open-circuit"));
	if (faults & POEMGR_FAULT_TYPE_OVER_CURRENT)
		json_object_array_add(arr, json_object_new_string("over-current"));
	if (faults & POEMGR_FAULT_TYPE_UNKNOWN)
		json_object_array_add(arr, json_object_new_string("unknown"));
	if (faults & POEMGR_FAULT_TYPE_CLASSIFICATION_ERROR)
		json_object_array_add(arr, json_object_new_string("classification-error"));

	return arr;
}

int poemgr_show(struct poemgr_ctx *ctx)
{
	struct json_object *root_obj, *ports_obj, *port_obj, *pse_arr, *pse_obj, *input_obj, *output_obj;
	struct poemgr_pse_chip *pse_chip;
	struct poemgr_metric metric_buf;
	struct poemgr_metric port_metric_buf;
	int metric = 0;
	char port_idx[3];
	int ret = 0;

	if(!ctx->profile->ready(ctx)) {
		fprintf(stderr, "Profile disabled. Enable profile first.\n");
		return 1;
	}

	/* Update port status */
	for (int p_idx = 0; p_idx < ctx->profile->num_ports; p_idx++) {
		ret = ctx->profile->update_port_status(ctx, p_idx);
		if (ret)
			return ret;
	}

	/* Update input status */
	ret = ctx->profile->update_input_status(ctx);
	if (ret)
		return ret;

	/* Update output status */
	ret = ctx->profile->update_output_status(ctx);
	if (ret)
		return ret;

	/* Create JSON object */
	root_obj = json_object_new_object();

	/* Add Profile name */
	json_object_object_add(root_obj, "profile", json_object_new_string(ctx->profile->name));

	/* Get PoE input information */
	input_obj = json_object_new_object();
	json_object_object_add(input_obj, "type", json_object_new_string(poemgr_poe_type_to_string(ctx->input_status.type)));
	json_object_object_add(root_obj, "input", input_obj);

	/* Get PoE output information */
	output_obj = json_object_new_object();

	json_object_object_add(output_obj, "power_budget", json_object_new_int(ctx->output_status.power_budget));
	json_object_object_add(output_obj, "type", json_object_new_string(poemgr_poe_type_to_string(ctx->output_status.type)));

	/* Get port information */
	ports_obj = json_object_new_object();
	for (int i = 0; i < ctx->profile->num_ports; i++) {
		snprintf(port_idx, 3, "%d", i);
		port_obj = json_object_new_object();
		json_object_object_add(port_obj, "enabled", json_object_new_boolean(!!ctx->ports[i].status.enabled));
		json_object_object_add(port_obj, "active", json_object_new_boolean(!!ctx->ports[i].status.active));
		json_object_object_add(port_obj, "poe_class", json_object_new_int(ctx->ports[i].status.poe_class));
		json_object_object_add(port_obj, "power", json_object_new_int(ctx->ports[i].status.power));
		json_object_object_add(port_obj, "power_limit", json_object_new_int(ctx->ports[i].status.power_limit));
		json_object_object_add(port_obj, "name", !!ctx->ports[i].settings.name ? json_object_new_string(ctx->ports[i].settings.name) : NULL);
		json_object_object_add(port_obj, "faults", poemgr_create_port_fault_array(ctx->ports[i].status.faults));
		if (ctx->profile->export_port_metric) {
			metric = 0;
			do {
				ret = ctx->profile->export_port_metric(ctx, i, &port_metric_buf, metric);
				if (ret)
					goto out;

				if (port_metric_buf.type == POEMGR_METRIC_END)
					break;

				switch (port_metric_buf.type) {
					case POEMGR_METRIC_INT32:
						json_object_object_add(port_obj, port_metric_buf.name, json_object_new_int(port_metric_buf.val_int32));
						break;
					case POEMGR_METRIC_UINT32:
						json_object_object_add(port_obj, port_metric_buf.name, json_object_new_int64(port_metric_buf.val_uint32));
						break;
					case POEMGR_METRIC_STRING:
						json_object_object_add(port_obj, port_metric_buf.name, json_object_new_string(port_metric_buf.val_char));
						break;
					default:
						ret = 1;
						goto out;
				}
				metric++;
			}
			while (port_metric_buf.type != POEMGR_METRIC_END);
		}

		json_object_object_add(ports_obj, port_idx, port_obj);
	}
	json_object_object_add(output_obj, "ports", ports_obj);

	json_object_object_add(root_obj, "output", output_obj);

	pse_arr = json_object_new_array();
	json_object_object_add(root_obj, "pse", pse_arr);
	for (int i = 0; i < ctx->profile->num_pse_chips; i++) {
		pse_chip = &ctx->profile->pse_chips[i];
		pse_obj = json_object_new_object();
		json_object_array_add(pse_arr, pse_obj);

		json_object_object_add(pse_obj, "model", json_object_new_string(pse_chip->model));

		for (int j = 0; j < pse_chip->num_metrics; j++) {
			ret = pse_chip->export_metric(pse_chip, &metric_buf, j);
			if (ret) {
				fprintf(stderr, "Error exporting metrics from chip\n");
				goto out;
			}
			// Break early if metric end is reached before num_metrics
			if (metric_buf.type == POEMGR_METRIC_END)
				break;

			/* ToDo handle memory in case of error */
			switch (metric_buf.type) {
				case POEMGR_METRIC_INT32:
					json_object_object_add(pse_obj, metric_buf.name, json_object_new_int(metric_buf.val_int32));
					break;
				case POEMGR_METRIC_UINT32:
					// cast to int64 to avoid integer overflow
					json_object_object_add(pse_obj, metric_buf.name, json_object_new_int64(metric_buf.val_uint32));
					break;
				case POEMGR_METRIC_STRING:
					json_object_object_add(pse_obj, metric_buf.name, json_object_new_string(metric_buf.val_char));
					break;
				default:
					ret = 1;
					goto out;
			}
		}
	}


	/* Save to char pointer */
	const char *c = json_object_to_json_string_ext(root_obj, JSON_C_TO_STRING_PRETTY);

	fprintf(stdout, "%s\n", c);

out:
	json_object_put(root_obj);
	return ret;
}

int poemgr_enable(struct poemgr_ctx *ctx)
{
	if (!ctx->profile->enable)
		return 0;

	return ctx->profile->enable(ctx);
}

int poemgr_disable(struct poemgr_ctx *ctx)
{
	if (!ctx->profile->disable)
		return 0;

	return ctx->profile->disable(ctx);
}

int poemgr_apply(struct poemgr_ctx *ctx)
{
	/* Implicitly enable profile. */
	poemgr_enable(ctx);

	/*
	 * The PoE chip might need a tiny moment before input detection.
	 * On a USW-Flex powered by an 802.3at injector (TL-POE160S), it initially
	 * reports a 802.3af input, which results in a low-balled power budget.
	 * After the following small nap, input is correctly read as 802.3at.
	 */
	usleep(10000);

	if (!ctx->profile->apply_config)
		return 0;

	return ctx->profile->apply_config(ctx);
}

int main(int argc, char *argv[])
{
	struct uci_context *uci_ctx = uci_alloc_context();
	static struct poemgr_profile *profile = NULL;
	struct poemgr_ctx ctx = {};
	char *action;
	size_t i;
	int ret;

	/* Default action */
	action = POEMGR_ACTION_STRING_SHOW;

	/* Load settings */
	ret = poemgr_load_settings(&ctx, uci_ctx);
	if (ret)
		exit(1);

	/* Select profile */
	for (i = 0; poemgr_profiles[i]; i++) {
		profile = poemgr_profiles[i];

		if (!strcmp(profile->name, ctx.settings.profile))
			break;
	}

	if (profile == NULL)
		exit(1);

	ctx.profile = profile;

	/* Load port settings (requires selected profile) */
	ret = poemgr_load_port_settings(&ctx, uci_ctx);
	if (ret)
		exit(1);

	/* Call profile init routine */
	if (profile->init(&ctx))
		exit(1);

	/* check which action we are supposed to perform */
	if (argc > 1)
		action = argv[1];

	if (!strcmp(POEMGR_ACTION_STRING_SHOW, action)) {
		/* Show */
		ret = poemgr_show(&ctx);
	} else if (!strcmp(POEMGR_ACTION_STRING_APPLY, action)) {
		/* Apply */
		ret = poemgr_apply(&ctx);
	} else if (!strcmp(POEMGR_ACTION_STRING_ENABLE, action)) {
		/* Enable */
		ret = poemgr_enable(&ctx);
	} else if (!strcmp(POEMGR_ACTION_STRING_DISABLE, action)) {
		/* Disable */
		ret = poemgr_disable(&ctx);
	} else {
		fprintf(stderr, "Unknown command.\n");
		ret = 1;
	}

	if (uci_ctx)
		uci_free_context(uci_ctx);

	return ret;
}
