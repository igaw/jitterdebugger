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

static void dump_samples(const char *filename)
{
	struct latency_sample sample;
	FILE *file;

	file = fopen(filename, "r");
	if (!file)
		return;

	while(fread(&sample, sizeof(struct latency_sample), 1, file)) {
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
	{ "version",	no_argument,		0,	 0  },
	{ 0, },
};

static void __attribute__((noreturn)) usage(int status)
{
	printf("jittersamples [options] FILE\n");
	printf("\n");
	printf("Usage:\n");
	printf("  -h, --help		Print this help\n");
	printf("      --version		Print version of jittersamples\n");

	exit(status);
}

int main(int argc, char *argv[])
{
	int long_idx;
	int c;

	while (1) {
		c = getopt_long(argc, argv, "h", long_options, &long_idx);
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
		default:
			printf("unknown option\n");
			usage(1);
		}
	}

	if (optind == argc) {
		printf("Missing filename\n");
		usage(1);
	}

	dump_samples(argv[optind]);

	return 0;
}
