# SPDX-License-Identifier: GPL-2.0-only

CC:=gcc
OUT:=poemgr
CFLAGS+= -Wall -Werror
CFLAGS+=$(shell pkg-config --cflags json-c)
LDLIBS+=$(shell pkg-config --libs json-c) -luci

all: $(OUT)

$(OUT):
	$(CC) $(CFLAGS) $(LDFLAGS) $(TARGET_ARCH) pd69104.c poemgr.c uswflex.c $(LDLIBS) -o $@

clean:
	rm $(OUT)

.PHONY: all clean
