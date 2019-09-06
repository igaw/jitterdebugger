// SPDX-License-Identifier: MIT

#define _GNU_SOURCE

#include <errno.h>
#include <string.h>

#include <hdf5.h>
#include <H5PTpublic.h>

#include "jitterdebugger.h"

#define min(x,y)                               \
({                                             \
       typeof(x) __x = (x);		       \
       typeof(x) __y = (y);		       \
       __x < __y ? __x : __y;                  \
})

#define BLOCK_SIZE 10000

struct cpu_data {
	hid_t set;
	uint64_t count;
	struct latency_sample *data;
};

static int output_hdf5(struct jd_samples_info *info, FILE *input)
{
	struct cpu_data *cpudata;
	struct latency_sample *data, *s, *d;
	hid_t file, type;
	herr_t err;
	uint64_t nrs, bs;
	size_t nr;
	off_t sz;
	unsigned int i, cnt;
	char *sid;
	char *ofile;

	if (asprintf(&ofile, "%s/samples.hdf5", info->dir) < 0)
		err_handler(errno, "asprintf()");

	if (fseeko(input, 0L, SEEK_END) < 0)
		err_handler(errno, "fseek()");
	sz = ftello(input);
	if (fseeko(input, 0L, SEEK_SET) < 0)
		err_handler(errno, "fseek()");

	nrs = sz / sizeof(struct latency_sample);
	bs = min(nrs, BLOCK_SIZE);

	data = malloc(sizeof(struct latency_sample) * bs);
	if (!data)
		err_handler(ENOMEM, "malloc()");

	file = H5Fcreate(ofile, H5F_ACC_TRUNC,
			H5P_DEFAULT, H5P_DEFAULT);
	if (file == H5I_INVALID_HID)
		err_handler(EIO, "failed to open file %s\n", ofile);

	type = H5Tcreate(H5T_COMPOUND, sizeof(struct latency_sample));
	if (type == H5I_INVALID_HID)
		err_handler(EIO, "failed to create compound HDF5 type");

	err = H5Tinsert(type, "CPUID", 0, H5T_NATIVE_UINT32);
	if (err < 0)
		err_handler(EIO,
			"failed to add type info to HDF5 compound type");
	err = H5Tinsert(type, "Seconds", 4, H5T_NATIVE_UINT64);
	if (err < 0)
		err_handler(EIO,
			"failed to add type info to HDF5 compound type");
	err = H5Tinsert(type, "Nanoseconds", 12, H5T_NATIVE_UINT64);
	if (err < 0)
		err_handler(EIO,
			"failed to add type info to HDF5 compound type");
	err = H5Tinsert(type, "Value", 20, H5T_NATIVE_UINT64);
	if (err < 0)
		err_handler(EIO,
			"failed to add type info to HDF5 compound type");

	cpudata = malloc(info->cpus_online * sizeof(struct cpu_data));
	if (!cpudata)
		err_handler(errno, "failed to allocated memory for cpu sets\n");

	for (i = 0; i < info->cpus_online; i++) {
		cpudata[i].count = 0;
		cpudata[i].data = malloc(BLOCK_SIZE * sizeof(struct latency_sample));

		if (asprintf(&sid, "cpu%d\n", i) < 0)
			err_handler(errno, "failed to create label\n");
		cpudata[i].set = H5PTcreate(file, sid, type, (hsize_t)bs, H5P_DEFAULT);
		free(sid);
		if (cpudata[i].set == H5I_INVALID_HID)
			err_handler(EIO, "failed to create HDF5 packet table");
	}

	for (;;) {
		nr = fread(data, sizeof(struct latency_sample), bs, input);
		if (nr != bs) {
			if (feof(input))
				break;
			if (ferror(input))
				err_handler(errno, "fread()");
		}

		for (i = 0; i < nr; i++) {
			s = &data[i];
			if (s->cpuid >= info->cpus_online) {
				fprintf(stderr, "invalid sample found (cpuid %d)\n",
					s->cpuid);
				continue;
			}

			cnt = cpudata[s->cpuid].count;
			d = &(cpudata[s->cpuid].data[cnt]);
			memcpy(d, s, sizeof(struct latency_sample));
			cpudata[s->cpuid].count++;
		}

		for (i = 0; i < info->cpus_online; i++) {
			H5PTappend(cpudata[i].set, cpudata[i].count, cpudata[i].data);
			cpudata[i].count = 0;
		}
	}

	free(data);

	for (i = 0; i < info->cpus_online; i++) {
		H5PTclose(cpudata[i].set);
		free(cpudata[i].data);
	}
	free(cpudata);

	H5Tclose(type);
	H5Fclose(file);
	free(ofile);

	return 0;
}

static struct jd_samples_ops hdf5_ops = {
	.name = "Hierarchical Data Format",
	.format = "hdf5",
	.output = output_hdf5,
};

static int hdf5_plugin_init(void)
{
	return jd_samples_register(&hdf5_ops);
}

static void hdf5_plugin_cleanup(void)
{
	jd_samples_unregister(&hdf5_ops);
}

JD_PLUGIN_DEFINE(hdf5_plugin_init, hdf5_plugin_cleanup);
