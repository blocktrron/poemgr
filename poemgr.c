#include <stdlib.h>
#include <string.h>
#include <uci.h>

#include "poemgr.h"

extern struct poemgr_profile poemgr_profile_uswflex;

static struct poemgr_profile *poemgr_profiles[] = {
	&poemgr_profile_uswflex,
	NULL
};

static int uci_lookup_option_int(struct uci_context* uci, struct uci_section* s,
								 const char* name)
{
	const char* str = uci_lookup_option_string(uci, s, name);
	return str == NULL ? -1 : atoi(str);
}

static int load_port_settings(struct poemgr_ctx *ctx, struct uci_context *uci_ctx)
{
	struct uci_package *package;
	struct uci_element *e;
	struct uci_section *s;
	int ret = 0;

	if (uci_load(uci_ctx, "poemgr", &package) != UCI_OK) {
		ret = -1;
		goto out;
	}

	uci_foreach_element(&package->sections, e) {
		struct uci_section *s = uci_to_section(e);
		const char *disabled;
		const char *port;
		const char *name;
		int port_idx;

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

int load_settings(struct poemgr_ctx *ctx)
{
	struct uci_context *uci_ctx = uci_alloc_context();
	struct uci_package *package;
	struct uci_section *section;
	const char *s;
	int enabled;
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

	ctx->settings.enabled = uci_lookup_option_int(uci_ctx, section, "enabled");
	if (ctx->settings.enabled == -1) {
		ret = -1;
		goto out;
	}


	s = uci_lookup_option_string(uci_ctx, section, "profile");
	if (!s) {
		ret = -1;
		goto out;
	}

	ctx->settings.profile = strdup(s);

	ret = load_port_settings(ctx, uci_ctx);
out:
	if (uci_ctx);
		uci_free_context(uci_ctx);

	return ret;
}

int main(int argc, char *argv[])
{
	static struct poemgr_profile *profile;
	struct poemgr_ctx ctx;
	char *action;
	int ret;

	/* Default action */
	action = POEMGR_ACTION_STRING_SHOW;

	/* Load settings */
	ret = load_settings(&ctx);
	if (ret)
		exit(1);

	/* Select profile */
	profile = poemgr_profiles[0];
	while (1) {
		if (!strcmp(profile->name, ctx.settings.profile) || profile == NULL) {
			break;
		}
	}

	if (profile == NULL)
		exit(1);

	ctx.profile = profile;
	if (profile->init(&ctx))
		exit(1);

	/* check which action we are supposed to perform */
	if (argc > 1)
		action = argv[1];
	
	if (!strcmp(POEMGR_ACTION_STRING_SHOW, action)) {
		/* Show */
	} else if (!strcmp(POEMGR_ACTION_STRING_APPLY, action)) {
		/* Apply */
	}

	return 0;
}
