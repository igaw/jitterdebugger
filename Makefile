# SPDX-License-Identifier: MIT

CFLAGS += -pthread -Wall -Wstrict-aliasing=1 -Wno-unused-result \
	  -Wsign-compare -Wtype-limits -Wmissing-prototypes \
	  -Wstrict-prototypes
LDFLAGS += -pthread

ifdef DEBUG
	CFLAGS += -O0 -g
	LDFLAGS += -g
else
	CFLAGS += -O2
endif

# Determine if HDF5 support will be built into jittersamples
JSCC=${CC}
ifneq ("$(wildcard /usr/include/hdf5.h)", "")
	CFLAGS += -DCONFIG_HDF5
	JSCC = h5cc -shlib
endif

TARGETS=jitterdebugger jittersamples

all: $(TARGETS)

jitterdebugger: jitterutils.o jitterwork.o jittersysinfo.o jitterdebugger.o

jittersamples: export HDF5_CC=${CC}
jittersamples: jitterutils.c jittersamples.c
	${JSCC} ${CFLAGS} -c jitterutils.c
	${JSCC} ${CFLAGS} -c jittersamples.c
	${JSCC} jitterutils.o jittersamples.o -o jittersamples

PHONY: .clean
clean:
	rm -f *.o
	rm -f $(TARGETS)
