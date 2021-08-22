CC:=gcc
OUT:=poemgr

all: poemgr

poemgr:
	$(CC) -luci -o $(OUT) pd69104.c poemgr.c uswflex.c

clean:
	rm $(OUT)