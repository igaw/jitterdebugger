// SPDX-License-Identifier: MIT

#include <unistd.h>
#include <getopt.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <inttypes.h>

#include <hdf5.h>
#include <H5PTpublic.h>

#include "jitterdebugger.h"

#define min(x,y)                               \
({                                             \
       typeof(x) __x = (x);		       \
       typeof(x) __y = (y);		       \
       __x < __y ? __x : __y;                  \
})

static void output_cvs(FILE *input, FILE *output)
{
	struct latency_sample sample;

	while(fread(&sample, sizeof(struct latency_sample), 1, input)) {
		fprintf(output, "%d;%lld.%.9ld;%" PRIu64 "\n",
			sample.cpuid,
			(long long)sample.ts.tv_sec,
			sample.ts.tv_nsec,
			sample.val);
	}
}

static void output_hdf5(FILE *input, const char *ofile)
{
	struct latency_sample *data;
	hid_t file, set, type;
	herr_t err;
	uint64_t nrs, bs;
	size_t nr;
	off_t sz;

	if (fseeko(input, 0L, SEEK_END) < 0)
		err_handler(errno, "fseek()");
	sz = ftello(input);
	if (fseeko(input, 0L, SEEK_SET) < 0)
		err_handler(errno, "fseek()");

	nrs = sz / sizeof(struct latency_sample);
	bs = min(nrs, 10000);

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

	set = H5PTcreate(file, "jitterdebugger",
			type, (hsize_t)bs, H5P_DEFAULT);
	if (set == H5I_INVALID_HID)
		err_handler(EIO,
			"failed to create HDF5 packet table");

	for (;;) {
		nr = fread(data, sizeof(struct latency_sample), bs, input);
		if (nr != bs) {
			if (feof(input))
				break;
			err_handler(errno, "fread()");
		}
		H5PTappend(set, bs, data);
	}

	free(data);

	H5PTclose(set);
	H5Tclose(type);
	H5Fclose(file);
}

static struct option long_options[] = {
	{ "help",	no_argument,		0,	'h' },
	{ "version",	no_argument,		0,	 0  },
	{ "format",	required_argument,	0,	'f' },
	{ "output",	required_argument,	0,	'o' },
	{ 0, },
};

static void __attribute__((noreturn)) usage(int status)
{
	printf("jittersamples [options] FILE\n");
	printf("  FILE                  Samples in raw format\n");
	printf("\n");
	printf("Usage:\n");
	printf("  -h, --help		Print this help\n");
	printf("      --version		Print version of jittersamples\n");
	printf("  -f, --format FMT	Exporting samples in format [cvs, hdf5]\n");
	printf("  -o, --output FILE	Write output into FILE\n");

	exit(status);
}

int main(int argc, char *argv[])
{
	FILE *input, *output;
	char *ifile, *ofile;
	int long_idx;
	int c;
	char *format = "cvs";

	ofile = "-";
	while (1) {
		c = getopt_long(argc, argv, "hf:o:", long_options, &long_idx);
		if (c < 0)
			break;

		switch (c) {
		case 0:
			if (!strcmp(long_options[long_idx].name, "version")) {
				printf("jittersamples %s\n",
					JD_VERSION);
				exit(0);
			}
			break;
		case 'h':
			usage(0);
		case 'f':
			format = optarg;
			break;
		case 'o':
			ofile = optarg;
			break;
		default:
			printf("unknown option\n");
			usage(1);
		}
	}

	if (optind == argc) {
		fprintf(stderr, "Missing filename\n");
		usage(1);
	}

	ifile = argv[optind];

	input = fopen(ifile, "r");
	if (!input)
		err_handler(errno, "fopen()");

	if (!strcmp(ofile, "-"))
		output = stdout;
	else
		output = fopen(ofile, "w");

	if (!strcmp(format, "cvs")) {
		output_cvs(input, output);
	} else if (!strcmp(format, "hdf5")) {
		if (!strcmp(ofile, "-")) {
			fprintf(stderr, "hdf5 format requires output file (--output)\n");
			exit(1);
		}
		output_hdf5(input, ofile);
	} else {
		fprintf(stderr, "Unsupported file format \"%s\"\n", format);
		exit(1);
	}

	fclose(input);
	fclose(output);

	return 0;
}
