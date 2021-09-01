#!/bin/sh /etc/rc.common

START=80
USE_PROCD=1

NAME=poemgr
PROG=/usr/bin/poemgr

. /lib/functions.sh


reload_service() {
	start
}

service_triggers() {
	procd_add_reload_trigger poemgr
}

stop_service()
{
	poemgr disable
}

start_service()
{
	DISABLED="$(uci -q get poemgr.@poemgr[-1].disabled)"
	DISABLED="${DISABLED:-1}"

	procd_open_instance
	procd_set_param command "$PROG"

	if [ "$DISABLED" -gt 0 ]
	then
		procd_append_param disable
	else
		procd_append_param apply
	fi
	procd_close_instance
}
