CFLAGS=-g -Wall -pthread -O3

all: jitterdebugger jittersamples

jitterdebugger: jitterutils.c jitterdebugger.c

jittersamples: jitterutils.c jittersamples.c

PHONY: .clean
clean:
	rm -f jitterdebugger
