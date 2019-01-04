// SPDX-License-Identifier: MIT

#include <unistd.h>
#include <getopt.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <inttypes.h>

#include "jitterdebugger.h"

static void dump_samples(const char *filename, int cpuid)
{
	struct latency_sample sample;
	FILE *file;

	file = fopen(filename, "r");
	if (!file)
		return;

	while(fread(&sample, sizeof(struct latency_sample), 1, file)) {
		if (cpuid >= 0 && sample.cpuid != cpuid)
			continue;

		printf("%d;%lld.%.9ld;%" PRIu64 "\n",
			sample.cpuid,
			(long long)sample.ts.tv_sec,
			sample.ts.tv_nsec,
			sample.val);
	}

	fclose(file);
}

static struct option long_options[] = {
	{ "help",	no_argument,		0,	'h' },
	{ "cpu",	required_argument,	0,	'c' },
	{ 0, },
};

static void __attribute__((noreturn)) usage(int status)
{
	printf("jittersamples [options] FILE\n");
	printf("\n");
	printf("Usage:\n");
	printf("  -h, --help		Print this help\n");
	printf("  -c, --cpu CPUID	Filter CPUID\n");

	exit(status);
}

int main(int argc, char *argv[])
{
	int cpuid = -1;
	long val;
	int c;

	while (1) {
		c = getopt_long(argc, argv, "hc:", long_options, NULL);
		if (c < 0)
			break;

		switch (c) {
		case 'h':
			usage(0);
		case 'c':
			val = parse_dec(optarg);
			if (val < 0)
				err_abort("Invalid value for CPUID. "
					  "Valid range is [0..]\n");
			cpuid = val;
			break;
		default:
			printf("unknown option\n");
			usage(1);
		}
	}

	if (optind == argc) {
		printf("Missing filename\n");
		usage(1);
	}

	dump_samples(argv[optind], cpuid);

	return 0;
}
