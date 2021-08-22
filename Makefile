CC:=gcc
OUT:=poemgr

all: poemgr

poemgr:
	$(CC) -o $(OUT) pd69104.c poemgr.c uswflex.c

clean:
	rm $(OUT)