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

jitterdebugger: jd_utils.o jd_work.o jd_sysinfo.o jitterdebugger.o


jittersamples_builtin_modules = jd_samples_csv

# Determine if HDF5 support will be built into jittersamples
JSCC=${CC}
ifneq ("$(wildcard /usr/include/hdf5.h)", "")
	CFLAGS += -DCONFIG_HDF5
	JSCC = h5cc -shlib
	jittersamples_builtin_modules += jd_samples_hdf5
endif

jittersamples_builtin_sources = $(addsuffix .c,$(jittersamples_builtin_modules))
jittersamples_builtin_objs = $(addsuffix .o,$(jittersamples_builtin_modules))

jittersamples_objs = jd_utils.o $(jittersamples_builtin_objs) \
	jd_samples_builtin.o jd_plugin.o jittersamples.o

jd_samples_builtin.c: scripts/genbuiltin $(jittersamples_builtin_sources)
	scripts/genbuiltin $(jittersamples_builtin_modules) > $@
$(jittersamples_objs): %.o: %.c
	export HDF5_CC=${CC}
	$(JSCC) ${CFLAGS} -D_FILENAME=$(basename $<) -c $< -o $@
jittersamples: $(jittersamples_objs)
	$(JSCC) $^ -o $@


PHONY: .clean
clean:
	rm -f *.o
	rm -f $(TARGETS)
