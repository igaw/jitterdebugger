CFLAGS=-g -Wall -pthread -O3

jitterdebugger: jitterdebugger.c

PHONY: .clean
clean:
	rm jitterdebugger
