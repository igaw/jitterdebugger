CFLAGS=-g -Wall -pthread -O3

all: jitterdebugger

jitterdebugger: jitterutils.c jitterdebugger.c

PHONY: .clean
clean:
	rm -f jitterdebugger
