# SPDX-License-Identifier: MIT

CFLAGS+=-Wall -Wstrict-aliasing=1 -Wno-unused-result -pthread -O2
LDFLAGS+=-pthread

TARGETS=jitterdebugger jittersamples

all: $(TARGETS)

jitterdebugger: jitterutils.o jitterdebugger.o

jittersamples: jitterutils.o jittersamples.o

PHONY: .clean
clean:
	rm -f *.o
	rm -f $(TARGETS)
