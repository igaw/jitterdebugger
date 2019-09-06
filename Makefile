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

TARGETS=jitterdebugger jittersamples

all: $(TARGETS)

jitterdebugger: jitterutils.o jitterwork.o jittersysinfo.o jitterdebugger.o


jittersamples_builtin_modules = jd_samples_csv jd_samples_hdf5
jittersamples_builtin_sources = $(addsuffix .c,$(jittersamples_builtin_modules))
jittersamples_builtin_objs = $(addsuffix .o,$(jittersamples_builtin_modules))

jittersamples_objs = jitterutils.o $(jittersamples_builtin_objs) \
	jd_samples_builtin.o jd_plugin.o jittersamples.o

jd_samples_builtin.c: scripts/genbuiltin $(jittersamples_builtin_sources)
	scripts/genbuiltin $(jittersamples_builtin_modules) > $@
$(jittersamples_objs): %.o: %.c
	export HDF5_CC=${CC}
	h5cc ${CFLAGS} -D_FILENAME=$(basename $<) -c $< -o $@
jittersamples: $(jittersamples_objs)
	h5cc -shlib $^ -o $@


PHONY: .clean
clean:
	rm -f *.o
	rm -f $(TARGETS)
