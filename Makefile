# SPDX-License-Identifier: MIT

CFLAGS += -Wall -Wstrict-aliasing=1 -Wno-unused-result -pthread
LDFLAGS += -pthread

ifdef DEBUG
	CFLAGS += -O0 -g
	LDFLAGS += -g
else
	CFLAGS += -O2
endif

TARGETS=jitterdebugger jittersamples

all: $(TARGETS)

jitterdebugger: jitterutils.o jitterwork.o jittersysinfo.o jitterdebugger.o

jittersamples: export HDF5_CC=${CC}
jittersamples: jitterutils.c jittersamples.c
	h5cc ${CFLAGS} -c jitterutils.c
	h5cc ${CFLAGS} -c jittersamples.c
	h5cc -shlib jitterutils.o jittersamples.o -o jittersamples

PHONY: .clean
clean:
	rm -f *.o
	rm -f $(TARGETS)
