CFLAGS+=-g -Wall -pthread -O3
LDFLAGS+=-pthread

TARGETS=jitterdebugger jittersamples

all: $(TARGETS)

jitterdebugger: jitterutils.o jitterdebugger.o

jittersamples: jitterutils.o jittersamples.o

PHONY: .clean
clean:
	rm -f *.o
	rm -f $(TARGETS)
