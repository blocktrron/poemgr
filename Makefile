# SPDX-License-Identifier: GPL-2.0-only


OUT:=poemgr
OBJ += common.o
OBJ += ip8008.o
OBJ += pd69104.o
OBJ += poemgr.o
OBJ += psx10.o
OBJ += uswflex.o

CC:=gcc
CFLAGS+= -Wall -Werror -MD -MP
CFLAGS+=$(shell pkg-config --cflags json-c)
LDLIBS+=$(shell pkg-config --libs json-c) -luci


all: $(OUT)

.SUFFIXES: .o .c
.c.o:
	$(CC) $(CFLAGS) $(CPPFLAGS) $(TARGET_ARCH) -c -o $@ $<

$(OUT): $(OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) $(TARGET_ARCH) $^ $(LDLIBS) -o $@

clean:
	rm -f $(OUT) $(OBJ) $(DEP)

# load dependencies
DEP = $(OBJ:.o=.d)
-include $(DEP)

.PHONY: all clean
