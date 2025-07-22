#!/bin/sh

[ -e /etc/config/poemgr ] && exit 0

. /lib/functions/uci-defaults.sh

board=$(board_name)
case "$board" in
plasmacloud,psx8|\
plasmacloud,psx10)
    cp /usr/lib/poemgr/config/psx10.config /etc/config/poemgr
    ;;
ubnt,usw-flex)
    cp /usr/lib/poemgr/config/usw-lite.config /etc/config/poemgr
    ;;
esac
