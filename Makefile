CFLAGS=-g -Wall -lpthread -O3

jitterdebugger: jitterdebugger.c

PHONY: .clean
clean:
	rm jitterdebugger
