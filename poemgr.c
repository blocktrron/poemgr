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
