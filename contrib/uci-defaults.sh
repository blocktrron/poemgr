#!/bin/sh

[ -e /etc/config/poemgr ] && exit 0

. /lib/functions/uci-defaults.sh

board=$(board_name)
case "$board" in
ubnt,usw-flex)
    cp /usr/lib/poemgr/config/usw-lite.config /etc/config/poemgr
    ;;
esac
