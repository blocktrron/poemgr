# SPDX-License-Identifier: GPL-2.0-only

CC:=gcc
OUT:=poemgr
CFLAGS+= -Wall -Werror
CFLAGS+=$(shell pkg-config --cflags json-c)
LDFLAGS+=$(shell pkg-config --libs json-c) -luci

all: $(OUT)

$(OUT):
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(OUT) pd69104.c poemgr.c uswflex.c

clean:
	rm $(OUT)
